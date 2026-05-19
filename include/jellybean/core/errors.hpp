#pragma once

#include <stdexcept>
#include <string>

namespace jellybean {
namespace core {

class JellybeanException : public std::runtime_error {
public:
    explicit JellybeanException(const std::string& msg) : std::runtime_error(msg) {}
};

class MemoryException : public JellybeanException {
public:
    explicit MemoryException(const std::string& msg) : JellybeanException("Memory Error: " + msg) {}
};

class NetworkException : public JellybeanException {
public:
    explicit NetworkException(const std::string& msg) : JellybeanException("Network Error: " + msg) {}
};

class InferenceException : public JellybeanException {
public:
    explicit InferenceException(const std::string& msg) : JellybeanException("Inference Error: " + msg) {}
};

class ConfigurationException : public JellybeanException {
public:
    explicit ConfigurationException(const std::string& msg) : JellybeanException("Configuration Error: " + msg) {}
};

class TimeoutException : public JellybeanException {
public:
    explicit TimeoutException(const std::string& msg) : JellybeanException("Timeout Error: " + msg) {}
};

class ServerException : public JellybeanException {
public:
    explicit ServerException(const std::string& msg) : JellybeanException("Server Error: " + msg) {}
};

} // namespace core
} // namespace jellybean
