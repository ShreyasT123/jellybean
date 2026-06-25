#include "jellybean/inference/torch_backend.hpp"

#include <torch/script.h>
#include <torch/torch.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jellybean::inference {

namespace {
using steady_clock_t = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;
}  // namespace

class TorchBackend final : public IInferenceBackend {
   public:
    bool load(const std::string& model_id, const std::string& model_path, // NOLINT(modernize-use-trailing-return-type,bugprone-easily-swappable-parameters)
              DeviceKind device) override {
        std::lock_guard lock(mu_);
        fprintf(stderr, "[TORCH_BACKEND] load called: model_id=%s path=%s device=%s\n",
                model_id.c_str(), model_path.c_str(), device == DeviceKind::Cuda ? "CUDA" : "CPU");
        if (models_.find(model_id) != models_.end()) {
            fprintf(stderr, "[TORCH_BACKEND] Model already loaded: %s\n", model_id.c_str());
            return true;
        }
        ModelState st;
        st.device = (device == DeviceKind::Cuda && torch::cuda::is_available()) ? torch::kCUDA
                                                                                : torch::kCPU;
        if (!model_path.empty()) {
            try {
                fprintf(stderr, "[TORCH_BACKEND] Loading model from %s\n", model_path.c_str());
                st.module = torch::jit::load(model_path, st.device);
                st.has_module = true;
                fprintf(stderr, "[TORCH_BACKEND] Model loaded successfully\n");
            } catch (const c10::Error& e) {
                fprintf(stderr, "[TORCH_BACKEND] torch::jit::load failed: %s\n", e.what());
                return false;
            } catch (const std::exception& e) {
                fprintf(stderr, "[TORCH_BACKEND] torch::jit::load failed: %s\n", e.what());
                return false;
            } catch (...) {
                fprintf(stderr, "[TORCH_BACKEND] torch::jit::load failed: unknown exception\n");
                return false;
            }
        }
        models_.emplace(model_id, std::move(st));
        fprintf(stderr, "[TORCH_BACKEND] Model %s registered (has_module=%d)\n", model_id.c_str(), st.has_module);
        return true;
    }

    bool unload(const std::string& model_id) override { // NOLINT(modernize-use-trailing-return-type)
        std::lock_guard lock(mu_);
        fprintf(stderr, "[TORCH_BACKEND] unload called: model_id=%s\n", model_id.c_str());
        bool result = models_.erase(model_id) > 0;
        fprintf(stderr, "[TORCH_BACKEND] unload result=%d\n", result);
        return result;
    }

    InferenceResponse infer(const InferenceRequest& req) override { // NOLINT(modernize-use-trailing-return-type)
        const auto t0 = steady_clock_t::now();
        InferenceResponse resp;

        ModelState st;
        {
            std::lock_guard lock(mu_);
            auto it = models_.find(req.model_id);
            if (it == models_.end()) {
                fprintf(stderr, "[TORCH_BACKEND] Model not loaded: %s\n", req.model_id.c_str());
                resp.error = "model not loaded: " + req.model_id;
                return resp;
            }
            st = it->second;
        }

        try {
            // No .clone() here, .to(device) will handle the move/copy to GPU if needed.
            // If device is CPU, from_blob is zero-copy (view), and if it's the target device,
            // no copy happens until the forward pass or if it is modified.
            torch::Tensor x = torch::from_blob(const_cast<float*>(req.input.data()), req.shape,
                                               torch::TensorOptions().dtype(torch::kFloat32))
                                  .to(st.device);

            torch::Tensor y;
            if (st.has_module) {
                std::vector<torch::jit::IValue> inputs;
                inputs.emplace_back(x);
                y = st.module.forward(inputs).toTensor();
            } else {
                y = torch::relu(x * 2.0f + 1.0f);
            }

            y = y.to(torch::kCPU); // contiguous() is often redundant after .to(kCPU)
            resp.shape.assign(y.sizes().begin(), y.sizes().end());

            auto numel = static_cast<size_t>(y.numel());
            if (numel > req.output_buffer.size()) {
                resp.error = "output buffer too small";
            } else {
                std::memcpy(req.output_buffer.data(), y.data_ptr<float>(), numel * sizeof(float));
                resp.output_elems_written = static_cast<uint32_t>(numel);
                resp.ok = true;
            }
        } catch (const c10::Error& e) {
            fprintf(stderr, "[TORCH_BACKEND] Torch error: %s\n", e.what_without_backtrace());
            resp.error = e.what_without_backtrace();
        } catch (const std::exception& e) {
            fprintf(stderr, "[TORCH_BACKEND] Exception: %s\n", e.what());
            resp.error = e.what();
        }

        const auto t1 = steady_clock_t::now();
        resp.latency_ns = static_cast<uint64_t>(std::chrono::duration_cast<ns>(t1 - t0).count());
        return resp;
    }

    std::vector<InferenceResponse> infer_batch(
        const std::vector<InferenceRequest>& batch) override { // NOLINT(modernize-use-trailing-return-type)
        const auto t0 = steady_clock_t::now();
        std::vector<InferenceResponse> responses(batch.size());
        if (batch.empty()) return responses;

        ModelState st;
        {
            std::lock_guard lock(mu_);
            auto it = models_.find(batch[0].model_id);
            if (it == models_.end()) {
                fprintf(stderr, "[TORCH_BACKEND] Model not loaded: %s\n", batch[0].model_id.c_str());
                for (auto& r : responses) r.error = "model not loaded: " + batch[0].model_id;
                return responses;
            }
            st = it->second;
        }

        try {
            std::vector<torch::Tensor> tensors;
            tensors.reserve(batch.size());
            for (size_t i = 0; i < batch.size(); ++i) {
                const auto& req = batch[i];
                tensors.push_back(torch::from_blob(const_cast<float*>(req.input.data()), req.shape,
                                                   torch::TensorOptions().dtype(torch::kFloat32))
                                      .to(st.device));
            }

            torch::Tensor x = torch::cat(tensors, 0);

            torch::Tensor y;
            if (st.has_module) {
                std::vector<torch::jit::IValue> inputs;
                inputs.emplace_back(x);
                y = st.module.forward(inputs).toTensor();
            } else {
                y = torch::relu(x * 2.0f + 1.0f);
            }

            y = y.to(torch::kCPU);

            // Chunk back to individual responses
            auto chunks = y.chunk(batch.size(), 0);
            for (size_t i = 0; i < batch.size(); ++i) {
                auto& chunk = chunks[i];
                responses[i].shape.assign(chunk.sizes().begin(), chunk.sizes().end());

                auto numel = static_cast<size_t>(chunk.numel());
                if (numel > batch[i].output_buffer.size()) {
                    responses[i].error = "output buffer too small";
                } else {
                    std::memcpy(batch[i].output_buffer.data(), chunk.data_ptr<float>(),
                                numel * sizeof(float));
                    responses[i].output_elems_written = static_cast<uint32_t>(numel);
                    responses[i].ok = true;
                }
                responses[i].latency_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<ns>(steady_clock_t::now() - t0).count());
            }
        } catch (const c10::Error& e) {
            fprintf(stderr, "[TORCH_BACKEND] Torch error: %s\n", e.what_without_backtrace());
            for (auto& r : responses) r.error = e.what_without_backtrace();
        } catch (const std::exception& e) {
            fprintf(stderr, "[TORCH_BACKEND] Exception: %s\n", e.what());
            for (auto& r : responses) r.error = e.what();
        }

        return responses;
    }

   private:
    struct ModelState {
        bool has_module{false};
        torch::jit::script::Module module{};
        c10::Device device{torch::kCPU};
    };

    std::mutex mu_;
    std::unordered_map<std::string, ModelState> models_;
};

auto make_torch_backend() -> std::shared_ptr<IInferenceBackend> {
    return std::make_shared<TorchBackend>();
}

}  // namespace jellybean::inference
