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

target_compile_options(jellybean PRIVATE
    -fno-exceptions -fno-rtti
)

target_compile_options(jellybean PUBLIC
    -Wall -Wextra -Wpedantic -Werror -march=native -Wno-c99-extensions -Wno-unused-parameter -Wno-nested-anon-types
)

target_link_libraries(jellybean PRIVATE pthread)

if(ENABLE_TORCH)
    target_link_libraries(jellybean_inference_torch PRIVATE jellybean "${TORCH_LIBRARIES}")
    target_compile_options(jellybean_inference_torch PRIVATE ${TORCH_CXX_FLAGS})
endif()
