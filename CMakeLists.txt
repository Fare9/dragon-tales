cmake_minimum_required(VERSION 3.20)
project(dragon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

# FetchContent must be included before any FetchContent_Declare calls
include(FetchContent)

# Catch2
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Testing
option(DRAGON_BUILD_TESTS "Build tests" ON)

add_subdirectory(lib)
add_subdirectory(c_shim)
add_subdirectory(examples/cpp)

if (DRAGON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()