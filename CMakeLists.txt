cmake_minimum_required(VERSION 3.20)
project(dragon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

# MSVC will not link objects that disagree on the C runtime (/MD vs /MDd) or on
# _ITERATOR_DEBUG_LEVEL, and LLVM's static libraries carry whichever their own
# build used. The Debug configuration defaults to /MDd - and `cmake --build
# build` with no --config means Debug under the Visual Studio generators - so
# against a non-Debug LLVM that is a wall of LNK2038 errors, one per LLVM
# object. Use the release runtime in every configuration instead: Debug stays
# what one wants from it (no optimisation, full symbols) and links.
if(MSVC AND LLVM_BUILD_TYPE AND NOT LLVM_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  message(STATUS "LLVM was built ${LLVM_BUILD_TYPE}: every configuration uses the "
                 "release C runtime (/MD) so that Debug can link it")
endif()

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

# Optional Z3 support for SMT-based indirect jump resolution.
# Pass -DDRAGON_USE_Z3=OFF to build without Z3.
option(DRAGON_USE_Z3 "Enable Z3 SMT solver for indirect-jump path discovery" ON)

if(DRAGON_USE_Z3)
    # System Z3 (apt) ships no CMake config file — use LLVM's FindZ3 module.
    find_package(Z3 REQUIRED)
    message(STATUS "Found Z3 ${Z3_VERSION_STRING} at ${Z3_LIBRARIES}")
    if(NOT TARGET z3::libz3)
        add_library(z3::libz3 UNKNOWN IMPORTED)
        set_target_properties(z3::libz3 PROPERTIES
            IMPORTED_LOCATION "${Z3_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${Z3_INCLUDE_DIR}"
        )
    endif()
endif()

# Optional Unicorn support for emulation-based semantic round-trip tests:
# execute raw bytes under Unicorn to get "expected" CPU state, execute the
# same bytes lifted -> LLVM IR -> JIT'd to get "actual" state, and diff them.
# Unlike Z3 this degrades gracefully instead of being REQUIRED - it's only
# used by opt-in tests (tests/semantic/), not core lifting, so a checkout
# without libunicorn-dev should still build cleanly.
# Pass -DDRAGON_USE_UNICORN=OFF to disable explicitly.
option(DRAGON_USE_UNICORN "Enable Unicorn-based semantic round-trip tests" ON)

if(DRAGON_USE_UNICORN)
    # Unicorn (apt) ships no CMake config file either - use pkg-config.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(UNICORN QUIET IMPORTED_TARGET unicorn)
    endif()
    if(UNICORN_FOUND)
        message(STATUS "Found Unicorn ${UNICORN_VERSION}")
    else()
        message(STATUS "Unicorn not found - semantic round-trip tests disabled (install libunicorn-dev to enable)")
        set(DRAGON_USE_UNICORN OFF)
    endif()
endif()

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
add_subdirectory(examples/obfush)
add_subdirectory(examples/obfusheader)
add_subdirectory(tool)

if (DRAGON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ---------------------------------------------------------------------------
# Installation / packaging
# ---------------------------------------------------------------------------
# Lays out a self-contained SDK under CMAKE_INSTALL_PREFIX:
#   lib/      libdragon + libdragon_c (LLVM is statically linked in; only Z3
#             is an external runtime dependency, bundled by the release CI)
#   bin/      the `disassembler` CLI (and the import .lib / .dll on Windows)
#   include/  the C++ headers (dragon/...) and the C-shim header (dragon/dragon_c.h)
#   share/dragon/python/  the cffi-based Python API (dragon.py)
# These rules are inert for a normal build - they run only on `cmake --install`.
include(GNUInstallDirs)

# Make the installed SDK relocatable: libdragon_c must find libdragon and the
# bundled libz3 next to it, and the CLI in bin/ must find both in ../lib - by
# relative path, not the build-tree absolutes CMake would otherwise bake in.
if(APPLE)
    set(_dragon_origin "@loader_path")
else()
    set(_dragon_origin "$ORIGIN")
endif()
set_target_properties(libdragon libdragon_c disassembler PROPERTIES
        INSTALL_RPATH "${_dragon_origin};${_dragon_origin}/../${CMAKE_INSTALL_LIBDIR}")

install(TARGETS libdragon libdragon_c
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})   # Windows DLLs land in bin/

install(TARGETS disassembler
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})

# C++ public headers (the whole dragon/ tree) and the C-shim header.
install(DIRECTORY include/dragon
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES c_shim/dragon_c.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dragon)

# Python API and top-level docs.
install(FILES python/dragon.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dragon/python)
install(FILES README.md IGNIL.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dragon
        OPTIONAL)