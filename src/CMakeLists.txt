add_library(jellybean STATIC
    memory/arena.cpp
    memory/huge_pages.cpp
    inference/runtime.cpp
    scheduler/scheduler.cpp
    reactor/reactor.cpp
    reactor/timer_wheel.cpp
    reactor/io_uring_backend.cpp
    reactor/epoll_backend.cpp
    actor/actor.cpp
    proto/codec.cpp
)

add_executable(jellybean_demo
    demo/runtime_demo.cpp
)

if(ENABLE_TORCH)
    add_library(jellybean_inference_torch STATIC
        inference/torch_backend.cpp
    )

    add_executable(jellybean_torch_smoke
        inference/torch_smoke.cpp
    )

    add_executable(jellybean_infer_demo
        demo/inference_demo.cpp
    )
    add_executable(jellybean_infer_server_demo
        demo/infer_server_demo.cpp
    )
endif()

target_include_directories(jellybean PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(jellybean PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions -fno-rtti>
)

target_compile_options(jellybean PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4324>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror -march=native -Wno-c99-extensions -Wno-unused-parameter -Wno-nested-anon-types>
)

if(NOT WIN32)
    target_link_libraries(jellybean PRIVATE pthread)
endif()

target_link_libraries(jellybean_demo PRIVATE jellybean)
if(WIN32)
    target_link_libraries(jellybean_demo PRIVATE ws2_32)
endif()

if(ENABLE_TORCH)
    target_link_libraries(jellybean_inference_torch PRIVATE jellybean "${TORCH_LIBRARIES}")
    target_compile_options(jellybean_inference_torch PRIVATE ${TORCH_CXX_FLAGS})
    if(MSVC)
        target_compile_options(jellybean_inference_torch PRIVATE /WX- /wd4267 /wd4702)
    endif()

    target_link_libraries(jellybean_torch_smoke PRIVATE jellybean "${TORCH_LIBRARIES}")
    target_compile_options(jellybean_torch_smoke PRIVATE ${TORCH_CXX_FLAGS})
    if(MSVC)
        target_compile_options(jellybean_torch_smoke PRIVATE /WX- /wd4267 /wd4702)
    endif()
    target_link_libraries(jellybean_infer_demo PRIVATE jellybean jellybean_inference_torch)
    target_compile_options(jellybean_infer_demo PRIVATE ${TORCH_CXX_FLAGS})
    target_link_libraries(jellybean_infer_server_demo PRIVATE jellybean jellybean_inference_torch)
    target_compile_options(jellybean_infer_server_demo PRIVATE ${TORCH_CXX_FLAGS})

    if(WIN32)
        target_link_libraries(jellybean_torch_smoke PRIVATE ws2_32)
        target_link_libraries(jellybean_infer_demo PRIVATE ws2_32)
        target_link_libraries(jellybean_infer_server_demo PRIVATE ws2_32)
        add_custom_command(TARGET jellybean_torch_smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<JOIN:$<TARGET_RUNTIME_DLLS:jellybean_torch_smoke>,;>"
                $<TARGET_FILE_DIR:jellybean_torch_smoke>
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LIBTORCH_ROOT}/lib"
                $<TARGET_FILE_DIR:jellybean_torch_smoke>
            COMMAND_EXPAND_LISTS
        )
        add_custom_command(TARGET jellybean_infer_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<JOIN:$<TARGET_RUNTIME_DLLS:jellybean_infer_demo>,;>"
                $<TARGET_FILE_DIR:jellybean_infer_demo>
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LIBTORCH_ROOT}/lib"
                $<TARGET_FILE_DIR:jellybean_infer_demo>
            COMMAND_EXPAND_LISTS
        )
        add_custom_command(TARGET jellybean_infer_server_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<JOIN:$<TARGET_RUNTIME_DLLS:jellybean_infer_server_demo>,;>"
                $<TARGET_FILE_DIR:jellybean_infer_server_demo>
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LIBTORCH_ROOT}/lib"
                $<TARGET_FILE_DIR:jellybean_infer_server_demo>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
