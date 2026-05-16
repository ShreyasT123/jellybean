#include <iostream>
#include <vector>

#include <torch/torch.h>

int main() {
    try {
        torch::manual_seed(42);

        torch::Tensor a = torch::rand({2, 3});
        torch::Tensor b = torch::rand({3, 4});
        torch::Tensor c = torch::matmul(a, b);

        std::cout << "torch version: " << TORCH_VERSION_MAJOR << "." << TORCH_VERSION_MINOR << "\n";
        std::cout << "cuda available: " << (torch::cuda::is_available() ? "yes" : "no") << "\n";
        std::cout << "result shape: [" << c.size(0) << ", " << c.size(1) << "]\n";
        std::cout << "result sum: " << c.sum().item<double>() << "\n";
        std::cout << "torch smoke ok\n";
        return 0;
    } catch (const c10::Error& e) {
        std::cerr << "libtorch failure: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "std failure: " << e.what() << "\n";
        return 1;
    }
}
