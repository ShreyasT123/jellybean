#pragma once
#include <memory>

#include "jellybean/inference/backend.hpp"

namespace jellybean::inference {

std::shared_ptr<IInferenceBackend> make_torch_backend();

}  // namespace jellybean::inference
