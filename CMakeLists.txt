cmake_minimum_required(VERSION 3.25)
project(jellybean VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include custom cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(Sanitizers)

option(ENABLE_TORCH "Enable LibTorch integration targets" ON)
set(LIBTORCH_ROOT "C:/deps/libtorch" CACHE PATH "Path to libtorch root")

if(ENABLE_TORCH)
    if(EXISTS "${LIBTORCH_ROOT}/share/cmake/Torch/TorchConfig.cmake")
        if(WIN32 AND NOT EXISTS "${LIBTORCH_ROOT}/lib/c10.lib")
            message(FATAL_ERROR
                "LIBTORCH_ROOT points to a non-Windows LibTorch build (missing c10.lib). "
                "Current directory appears to contain Linux .so binaries. "
                "Download LibTorch for Windows (libtorch-win-shared-with-deps) and update LIBTORCH_ROOT.")
        endif()
        list(APPEND CMAKE_PREFIX_PATH "${LIBTORCH_ROOT}")
        find_package(Torch REQUIRED)
        message(STATUS "LibTorch enabled from: ${LIBTORCH_ROOT}")
    else()
        message(FATAL_ERROR "ENABLE_TORCH=ON but TorchConfig.cmake not found under LIBTORCH_ROOT=${LIBTORCH_ROOT}")
    endif()
endif()

# Dependencies are resolved through vcpkg/toolchain and direct find_package calls.

# Add subdirectories
add_subdirectory(src)

# Enable testing
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()

# Enable benchmarks
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
    add_subdirectory(benchmarks)
endif()
