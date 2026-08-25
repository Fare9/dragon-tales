if(DRAGON_USE_UNICORN)
    # Captures a real Unicorn trace of sum_to_n() from the real obfusheader.h
    # binary, lifts it, and shows LLVM's optimizer working on it - see
    # ToDo/Roadmap.md item 18 for the full writeup, including the SSE2
    # scalar-double IGNIL support this needed first.
    add_executable(obfusheader_trace_recovery obfusheader_trace_recovery.cpp)

    target_link_libraries(obfusheader_trace_recovery PRIVATE dragon::libdragon)

    target_include_directories(obfusheader_trace_recovery PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )

    llvm_map_components_to_libnames(OBFUSHEADER_JIT_LIBS orcjit native)
    target_link_libraries(obfusheader_trace_recovery PRIVATE
            PkgConfig::UNICORN ${OBFUSHEADER_JIT_LIBS}
    )
endif()
