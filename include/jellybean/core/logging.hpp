#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

namespace jellybean {
namespace core {

class Logger {
   public:
    static auto instance() -> Logger& {
        static Logger inst;
        return inst;
    }

    auto is_debug() const -> bool {
        return debug_;
    }

    void set_debug(bool d) {
        debug_ = d;
    }

    template <typename T>
    auto operator<<(const T& msg) -> Logger& {
        if (debug_) {
            std::cerr << msg;
        }
        return *this;
    }

    // Support for std::endl
    auto operator<<(std::ostream& (*manip)(std::ostream&)) -> Logger& {
        if (debug_) {
            manip(std::cerr);
        }
        return *this;
    }

   private:
    Logger() {
        const char* env_debug = std::getenv("DEBUG");
        if (env_debug != nullptr) {
            std::string val(env_debug);
            if (val == "1" || val == "true" || val == "TRUE") {
                debug_ = true;
            }
        }
    }
    bool debug_ = false;
};

#define JELLY_LOG_DEBUG jellybean::core::Logger::instance() << "[DEBUG] "
#define JELLY_LOG_INFO jellybean::core::Logger::instance() << "[INFO] "
#define JELLY_LOG_ERROR std::cerr << "[ERROR] "

}  // namespace core
}  // namespace jellybean
