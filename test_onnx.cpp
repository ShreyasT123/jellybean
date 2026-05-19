#include <onnxruntime_cxx_api.h>

#include <iostream>
#include <vector>

int main() {
    // 1. Silent + CPU-only environment
    Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "SimpleInfer");

    // 2. Session options (CPU optimized)
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(2);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    const char* model_path = "model.onnx";

    // 3. Load model (CPU execution)
    Ort::Session session(env, model_path, session_options);

    // 4. Input
    std::vector<float> input_tensor_values(512, 1.0f);
    std::vector<int64_t> input_shape = {1, 512};

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(),
        input_shape.size());

    // 5. Names
    Ort::AllocatorWithDefaultOptions allocator;

    auto input_name_alloc = session.GetInputNameAllocated(0, allocator);
    auto output_name_alloc = session.GetOutputNameAllocated(0, allocator);

    const char* input_names[] = {input_name_alloc.get()};
    const char* output_names[] = {output_name_alloc.get()};

    // 6. Inference
    try {
        auto output_tensors =
            session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

        float* output = output_tensors.front().GetTensorMutableData<float>();
        std::cout << "Output: " << output[0] << std::endl;

    } catch (const Ort::Exception& e) {
        std::cerr << "Inference failed: " << e.what() << std::endl;
    }

    return 0;
}