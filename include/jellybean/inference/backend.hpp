#pragma once
#include <string>
#include "jellybean/inference/types.hpp"

namespace jellybean::inference {

class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;

    virtual bool load(const std::string& model_id, const std::string& model_path, DeviceKind device) = 0;
    virtual bool unload(const std::string& model_id) = 0;
    virtual InferenceResponse infer(const InferenceRequest& req) = 0;
};

} // namespace jellybean::inference
