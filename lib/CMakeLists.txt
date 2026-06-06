add_library(libdragon SHARED
        Architecture.cpp
        Instruction.cpp
        BasicBlock.cpp
        Function.cpp
        Graph.cpp
        IGNIL.cpp
)

add_library(dragon::libdragon ALIAS libdragon)

set_target_properties(libdragon PROPERTIES
        OUTPUT_NAME dragon
)

target_compile_features(libdragon PUBLIC cxx_std_20)

# LLVM includes
target_include_directories(libdragon
        PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LLVM_INCLUDE_DIRS}
)

# LLVM definitions (e.g. _DEBUG, NDEBUG)
target_compile_definitions(libdragon PRIVATE ${LLVM_DEFINITIONS})

# LLVM components needed
llvm_map_components_to_libnames(LLVM_LIBS
        MC
        MCDisassembler
        MCParser
        Object
        Support
        X86
        X86Desc
        X86Info
        ARM
        ARMDesc
        ARMInfo
        AArch64
        AArch64Desc
        AArch64Info
        RISCV
        RISCVDesc
        RISCVInfo
)

target_link_libraries(libdragon
        PRIVATE
        ${LLVM_LIBS}
)

add_subdirectory(disassemblers)
add_subdirectory(assemblers)