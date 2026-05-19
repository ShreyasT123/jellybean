set(JELLYBEAN_SOURCES
    memory/arena.cpp
    memory/huge_pages.cpp
    inference/runtime.cpp
    scheduler/scheduler.cpp
    reactor/reactor.cpp
    reactor/timer_wheel.cpp
    actor/actor.cpp
    proto/codec.cpp
    reactor/io_uring_backend.cpp
    reactor/epoll_backend.cpp
    telemetry/metrics.cpp
    net/async_socket.cpp
    model/model_config.cpp
    model/model_registry.cpp
    model/model_repository.cpp
    model/model_executor.cpp
    reactor/continuation_pool.cpp
    telemetry/metrics_export.cpp
)

add_library(jellybean STATIC ${JELLYBEAN_SOURCES})

add_executable(jellybean_server server/main.cpp)
target_link_libraries(jellybean_server PRIVATE jellybean)

if(ENABLE_TORCH)
    add_library(jellybean_inference_torch STATIC
        inference/torch_backend.cpp
    )
    target_link_libraries(jellybean_server PRIVATE jellybean_inference_torch)
endif()

target_include_directories(jellybean PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

# Note: exceptions enabled — inference backend and model loading rely on them
target_compile_options(jellybean PRIVATE
    -Wall -Wextra
)

target_compile_options(jellybean PUBLIC
    -Wall -Wextra -Wpedantic -march=native
)

target_link_libraries(jellybean PRIVATE pthread)
find_package(spdlog REQUIRED)
add_compile_definitions(SPDLOG_USE_STD_FORMAT)
target_link_libraries(jellybean PUBLIC spdlog::spdlog_header_only)

if(ENABLE_TORCH)
    target_link_libraries(jellybean_inference_torch PRIVATE jellybean "${TORCH_LIBRARIES}")
    target_compile_options(jellybean_inference_torch PRIVATE ${TORCH_CXX_FLAGS})
endif()
