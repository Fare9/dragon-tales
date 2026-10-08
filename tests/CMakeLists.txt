add_executable(dragon_tests
        TestMain.cpp
        disassemblers/TestDisassembler.cpp
        disassemblers/TestX86Analysis.cpp
        disassemblers/TestX86IGNILLifter.cpp
        disassemblers/TestARMAnalysis.cpp
        disassemblers/TestARMIGNILLifter.cpp
        disassemblers/TestTraceLifter.cpp
        assemblers/TestAssembler.cpp
        analysis/TestBackwardSlicer.cpp
        analysis/TestSwitchDetector.cpp
        analysis/TestIGNILSimplify.cpp
        analysis/TestDeadStoreElimination.cpp
        sigs/TestPatternParser.cpp
        sigs/TestSignatureMatcher.cpp
        obfush/TestObfush.cpp
        lifters/TestLLVMLifter.cpp
        loader/TestLoader.cpp
)

if(DRAGON_USE_Z3)
    target_sources(dragon_tests PRIVATE
        analysis/TestSliceExprZ3.cpp
        obfush/TestObfushZ3.cpp
        symbolic/TestSymbolicExecutor.cpp
        symbolic/TestSymbolicCFG.cpp)
endif()

if(DRAGON_USE_UNICORN)
    target_sources(dragon_tests PRIVATE
        semantic/TestX86SemanticRoundTrip.cpp
        toy-vm/TestToyVM.cpp
        toy-vm/TestToyVMHandlerRecovery.cpp
    )
endif()

target_link_libraries(dragon_tests
        PRIVATE
        dragon::libdragon
        Catch2::Catch2WithMain
)

# The test sources call LLVM APIs directly (raw_ostream, Module, verifyFunction,
# etc.) and are compiled regardless of the optional features. libdragon links
# LLVM PRIVATEly, so on Windows those symbols are not re-exported through
# dragon::libdragon and the test binary must link the LLVM components itself.
# (On Linux the shared libdragon re-exports them, so this was never needed.)
# Mirror libdragon's component set.
llvm_map_components_to_libnames(TEST_LLVM_LIBS
        Core
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
target_link_libraries(dragon_tests PRIVATE ${TEST_LLVM_LIBS})

if(DRAGON_USE_Z3)
    target_link_libraries(dragon_tests PRIVATE z3::libz3)
endif()

if(DRAGON_USE_UNICORN)
    # orcjit/native: only the semantic round-trip tests JIT-execute lifted
    # IR, so this stays test-only rather than a libdragon dependency.
    llvm_map_components_to_libnames(LLVM_JIT_LIBS orcjit native)
    target_link_libraries(dragon_tests PRIVATE PkgConfig::UNICORN ${LLVM_JIT_LIBS})
endif()

target_include_directories(dragon_tests
        PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

include(CTest)
include(Catch)
catch_discover_tests(dragon_tests)