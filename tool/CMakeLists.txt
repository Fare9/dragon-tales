# The command-line tools call LLVM APIs directly (object-file loading, target
# registry, X86 target init, etc.). libdragon links LLVM PRIVATEly, so those
# symbols are baked into dragon.dll and not re-exported: on Windows a DLL
# exports nothing unless marked, so the tools cannot borrow LLVM through
# dragon::libdragon and must link the LLVM component libraries themselves.
# (On Linux this happened to work because shared objects export all symbols by
# default.) Mirror the component set libdragon uses.
llvm_map_components_to_libnames(TOOL_LLVM_LIBS
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

add_executable(disassembler disassembler.cpp)

target_link_libraries(disassembler PRIVATE dragon::libdragon ${TOOL_LLVM_LIBS})

target_include_directories(disassembler PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)

add_executable(opcode_audit opcode_audit.cpp)

target_link_libraries(opcode_audit PRIVATE dragon::libdragon ${TOOL_LLVM_LIBS})

target_include_directories(opcode_audit PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)
