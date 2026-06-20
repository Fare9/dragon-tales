cmake_minimum_required(VERSION 3.20)
project(dragon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

# Hack for using clang+llvm-18.1.8-x86_64-pc-windows-msvc.tar.xz on windows.
if(WIN32 AND TARGET LLVMDebugInfoPDB)
  get_target_property(_pdb_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
  if(_pdb_libs MATCHES "diaguids\\.lib")
    find_file(DIAGUIDS_LIB diaguids.lib PATHS
      "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2022/Professional/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2022/Enterprise/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/DIA SDK/lib/amd64"
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/DIA SDK/lib/amd64"
      NO_DEFAULT_PATH
    )
    if(DIAGUIDS_LIB)
      string(REGEX REPLACE "[^;]*diaguids\\.lib" "${DIAGUIDS_LIB}" _pdb_libs "${_pdb_libs}")
      set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${_pdb_libs}")
      message(STATUS "Resolved diaguids.lib -> ${DIAGUIDS_LIB}")
    else()
      message(WARNING "diaguids.lib (DIA SDK) not found; install the 'C++ profiling tools' / DIA SDK component via the Visual Studio Installer")
    endif()
  endif()
endif()

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