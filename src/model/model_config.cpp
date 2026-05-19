#include "jellybean/model/model_config.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace jellybean::model {

namespace {

// Trim whitespace + \r
auto trim(std::string s) -> std::string {
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    auto last = s.find_last_not_of(ws);
    if (last != std::string::npos) s.erase(last + 1);
    return s;
}

auto parse_shape(const std::string& val) -> std::vector<int64_t> {
    std::vector<int64_t> shape;
    std::stringstream ss(val);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) shape.push_back(std::stoll(item));
    }
    return shape;
}

auto parse_backend(const std::string& val) -> BackendKind {
    if (val == "torchscript" || val == "torch") return BackendKind::TorchScript;
    if (val == "onnx")                           return BackendKind::Onnx;
    return BackendKind::TorchScript;  // default
}

/**
 * Scan `version_dir` for a model file.
 * Returns the path of the first match: model.pt, model.onnx, model.engine, model.bin
 * Returns "" if none found.
 */
auto find_model_file(const fs::path& version_dir) -> std::string {
    static const char* candidates[] = {
        "model.pt", "model.pth", "model.onnx", "model.engine", "model.bin"
    };
    for (const char* name : candidates) {
        fs::path p = version_dir / name;
        if (fs::exists(p)) return p.string();
    }
    return {};
}

}  // namespace

// ─── ModelConfig::from_dir ──────────────────────────────────────────────────

auto ModelConfig::from_dir(const std::string& model_dir) -> ModelConfig {
    ModelConfig cfg;
    fs::path dir(model_dir);

    // Default model name = directory name
    cfg.name = dir.filename().string();

    // Parse config.ini
    fs::path config_path = dir / "config.ini";
    if (fs::exists(config_path)) {
        std::ifstream file(config_path);
        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = trim(line.substr(0, pos));
            std::string val = trim(line.substr(pos + 1));

            if      (key == "name")                 cfg.name = val;
            else if (key == "backend")              cfg.backend = parse_backend(val);
            else if (key == "version")              cfg.version = std::stoi(val);
            else if (key == "device")               cfg.device = (val == "cuda" ? inference::DeviceKind::Cuda : inference::DeviceKind::Cpu);
            else if (key == "max_batch_size")       cfg.max_batch_size = static_cast<std::size_t>(std::stoul(val));
            else if (key == "max_batch_delay_us")   cfg.max_batch_delay_us = std::stoll(val);
            else if (key == "input_shape")          cfg.input_shape = parse_shape(val);
        }
    }

    // Resolve versioned model file: <model_dir>/<version>/model.*
    fs::path version_dir = dir / std::to_string(cfg.version);
    if (fs::is_directory(version_dir)) {
        cfg.model_file_path = find_model_file(version_dir);
    }

    // Fallback: scan all numeric version directories for the highest available
    if (cfg.model_file_path.empty()) {
        int best_ver = -1;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_directory()) continue;
            try {
                int v = std::stoi(entry.path().filename().string());
                if (v > best_ver) {
                    std::string candidate = find_model_file(entry.path());
                    if (!candidate.empty()) {
                        best_ver              = v;
                        cfg.version           = v;
                        cfg.model_file_path   = candidate;
                    }
                }
            } catch (...) {}
        }
    }

    return cfg;
}

}  // namespace jellybean::model
