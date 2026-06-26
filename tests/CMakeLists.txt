add_executable(dragon_tests
        TestMain.cpp
        disassemblers/TestDisassembler.cpp
        disassemblers/TestX86Analysis.cpp
        disassemblers/TestX86IGNILLifter.cpp
        assemblers/TestAssembler.cpp
        analysis/TestBackwardSlicer.cpp
        analysis/TestSwitchDetector.cpp
)

if(DRAGON_USE_Z3)
    target_sources(dragon_tests PRIVATE analysis/TestSliceExprZ3.cpp)
endif()

target_link_libraries(dragon_tests
        PRIVATE
        dragon::libdragon
        Catch2::Catch2WithMain
)

if(DRAGON_USE_Z3)
    target_link_libraries(dragon_tests PRIVATE z3::libz3)
endif()

target_include_directories(dragon_tests
        PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

include(CTest)
include(Catch)
catch_discover_tests(dragon_tests)