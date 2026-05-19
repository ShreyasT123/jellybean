#pragma once

#include <cstdlib>
#include <string>
#include <cstdio>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace jellybean::core {

inline void ensure_spdlog_init() {
    static bool init = []() {
        try {
            // Initialize thread pool: 8192 max items, 1 worker thread
            spdlog::init_thread_pool(8192, 1);
            
            // Create async console logger
            auto async_logger = spdlog::stdout_color_mt<spdlog::async_factory>("async_logger");
            spdlog::set_default_logger(async_logger);
            
            // Configure automatic flushing on error/critical to prevent loss during failures
            async_logger->flush_on(spdlog::level::err);
            
            // Set pattern
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
            
            // Set level based on DEBUG environment variable
            const char* env_debug = std::getenv("DEBUG");
            bool debug = false;
            if (env_debug != nullptr) {
                std::string val(env_debug);
                if (val == "1" || val == "true" || val == "TRUE") {
                    debug = true;
                }
            }
            if (debug) {
                spdlog::set_level(spdlog::level::debug);
            } else {
                spdlog::set_level(spdlog::level::info);
            }
        } catch (const spdlog::spdlog_ex& ex) {
            std::fprintf(stderr, "Log initialization failed: %s\n", ex.what());
        }
        return true;
    }();
    (void)init;
}

// Helper structure to trigger dynamic spdlog initialization on first use
struct LogInitTrigger {
    LogInitTrigger() {
        ensure_spdlog_init();
    }
};

}  // namespace jellybean::core

// Redefine JELLY_LOG macros to use format strings directly for zero-allocation performance.
// These instantiate a static local LogInitTrigger to guarantee thread-safe initialization on the first log.
#define JELLY_LOG_DEBUG(...) \
    do { \
        static jellybean::core::LogInitTrigger _trigger; \
        (void)_trigger; \
        spdlog::debug(__VA_ARGS__); \
    } while (0)

#define JELLY_LOG_INFO(...) \
    do { \
        static jellybean::core::LogInitTrigger _trigger; \
        (void)_trigger; \
        spdlog::info(__VA_ARGS__); \
    } while (0)

#define JELLY_LOG_ERROR(...) \
    do { \
        static jellybean::core::LogInitTrigger _trigger; \
        (void)_trigger; \
        spdlog::error(__VA_ARGS__); \
    } while (0)
