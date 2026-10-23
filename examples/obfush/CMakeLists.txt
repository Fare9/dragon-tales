if(DRAGON_USE_Z3)
    # Runs Disassembler -> IGNIL -> BackwardSlicer -> Z3 against real
    # compiled bytes and prints the result - see obfush_repro.c and
    # ToDo/Roadmap.md item 14 for the full writeup.
    add_executable(obfush_constant_recovery obfush_constant_recovery.cpp)

    target_link_libraries(obfush_constant_recovery PRIVATE dragon::libdragon z3::libz3)

    target_include_directories(obfush_constant_recovery PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )
endif()

if(DRAGON_USE_UNICORN)
    # Captures a real Unicorn trace of compute()+VmDispatch, lifts it, and
    # shows LLVM's optimizer recovering the real `a + b` - see
    # ToDo/Roadmap.md item 14/15 for the full writeup, including the 5
    # X86IGNILLifter gaps that had to be fixed first.
    add_executable(obfush_full_recovery obfush_full_recovery.cpp)

    target_link_libraries(obfush_full_recovery PRIVATE dragon::libdragon)

    target_include_directories(obfush_full_recovery PRIVATE
            ${PROJECT_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )

    llvm_map_components_to_libnames(OBFUSH_JIT_LIBS orcjit native)
    target_link_libraries(obfush_full_recovery PRIVATE
            PkgConfig::UNICORN ${OBFUSH_JIT_LIBS}
    )
endif()
