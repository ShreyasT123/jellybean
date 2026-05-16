#include "jellybean/inference/torch_backend.hpp"

#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <torch/torch.h>
#include <torch/script.h>

namespace jellybean::inference {

namespace {
using steady_clock_t = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;
}

class TorchBackend final : public IInferenceBackend {
public:
    bool load(const std::string& model_id, const std::string& model_path, DeviceKind device) override {
        std::lock_guard lock(mu_);
        if (models_.find(model_id) != models_.end()) {
            return true;
        }
        ModelState st;
        st.device = (device == DeviceKind::Cuda && torch::cuda::is_available()) ? torch::kCUDA : torch::kCPU;
        if (!model_path.empty()) {
            try {
                st.module = torch::jit::load(model_path, st.device);
                st.has_module = true;
            } catch (...) {
                return false;
            }
        }
        models_.emplace(model_id, std::move(st));
        return true;
    }

    bool unload(const std::string& model_id) override {
        std::lock_guard lock(mu_);
        return models_.erase(model_id) > 0;
    }

    InferenceResponse infer(const InferenceRequest& req) override {
        const auto t0 = steady_clock_t::now();
        InferenceResponse resp;

        ModelState st;
        {
            std::lock_guard lock(mu_);
            auto it = models_.find(req.model_id);
            if (it == models_.end()) {
                resp.error = "model not loaded: " + req.model_id;
                return resp;
            }
            st = it->second;
        }

        try {
            auto opts = torch::TensorOptions().dtype(torch::kFloat32).device(st.device);
            torch::Tensor x = torch::from_blob(
                const_cast<float*>(req.input.data()),
                req.shape,
                torch::TensorOptions().dtype(torch::kFloat32)).clone().to(st.device);

            torch::Tensor y;
            if (st.has_module) {
                std::vector<torch::jit::IValue> inputs;
                inputs.emplace_back(x);
                y = st.module.forward(inputs).toTensor();
            } else {
                y = torch::relu(x * 2.0f + 1.0f);
            }

            y = y.to(torch::kCPU).contiguous();
            resp.shape.assign(y.sizes().begin(), y.sizes().end());
            resp.output.resize(static_cast<size_t>(y.numel()));
            std::memcpy(resp.output.data(), y.data_ptr<float>(), resp.output.size() * sizeof(float));
            resp.ok = true;
        } catch (const c10::Error& e) {
            resp.error = e.what_without_backtrace();
        } catch (const std::exception& e) {
            resp.error = e.what();
        }

        const auto t1 = steady_clock_t::now();
        resp.latency_ns = static_cast<uint64_t>(std::chrono::duration_cast<ns>(t1 - t0).count());
        return resp;
    }

private:
    struct ModelState {
        bool has_module{false};
        torch::jit::script::Module module;
        c10::Device device{torch::kCPU};
    };

    std::mutex mu_;
    std::unordered_map<std::string, ModelState> models_;
};

std::shared_ptr<IInferenceBackend> make_torch_backend() {
    return std::make_shared<TorchBackend>();
}

} // namespace jellybean::inference
