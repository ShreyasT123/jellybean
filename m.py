import torch
import torch.nn as nn


# simple model
class SimpleNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(nn.Linear(512, 256), nn.ReLU(), nn.Linear(256, 128))

    def forward(self, x):
        return self.net(x)


model = SimpleNet()
model.eval()

# dummy input (batch=1, 512 features)
dummy_input = torch.randn(1, 512)

# export
torch.onnx.export(
    model,
    dummy_input,
    "model.onnx",
    input_names=["input"],
    output_names=["output"],
    dynamic_axes={"input": {0: "batch_size"}, "output": {0: "batch_size"}},
    opset_version=17,
)

print("ONNX exported: model.onnx")
https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-1.26.0.tgz
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <iostream>

int main() {
    // 1. Initialize the Environment
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "SimpleInfer");

    // 2. Set Session Options and Load the Model
    Ort::SessionOptions session_options;
    const char* model_path = "your_model.onnx";
    Ort::Session session(env, model_path, session_options);

    // 3. Prepare Input Data
    // (Assuming model takes 1 input of shape 1x3, e.g., 3 float values)
    std::vector<float> input_tensor_values = {1.0f, 2.0f, 3.0f};
    std::vector<int64_t> input_shape = {1, 3};

    // 4. Create Input Tensor
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault
    );
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), 
        input_shape.data(), input_shape.size()
    );

    // 5. Setup Input/Output Names
    Ort::AllocatorWithDefaultOptions allocator;
    char* input_name = session.GetInputNameAllocated(0, allocator);
    char* output_name = session.GetOutputNameAllocated(0, allocator);
    
    std::vector<const char*> input_names = {input_name};
    std::vector<const char*> output_names = {output_name};

    // 6. Run Inference
    try {
        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr}, 
            input_names.data(), &input_tensor, 1, 
            output_names.data(), 1
        );

        // 7. Extract and print results
        float* float_output = output_tensors.front().GetTensorMutableData<float>();
        std::cout << "Inference successful! Output value: " << float_output[0] << std::endl;
    } 
    catch (const Ort::Exception& e) {
        std::cerr << "Inference failed: " << e.what() << std::endl;
    }

    // Cleanup allocated names
    allocator.Free(input_name);
    allocator.Free(output_name);

    return 0;
}