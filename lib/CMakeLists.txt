add_library(libdragon SHARED
        Architecture.cpp
        Instruction.cpp
        BasicBlock.cpp
        Function.cpp
        Graph.cpp
        IGNIL.cpp
        LLVMLifter.cpp
)

add_library(dragon::libdragon ALIAS libdragon)

set_target_properties(libdragon PROPERTIES
        OUTPUT_NAME dragon
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_compile_features(libdragon PUBLIC cxx_std_20)

# LLVM includes
target_include_directories(libdragon
        PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        ${LLVM_INCLUDE_DIRS}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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
        Passes
        TransformUtils
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
add_subdirectory(analysis)