add_executable(disassembler disassembler.cpp)

target_link_libraries(disassembler PRIVATE dragon::libdragon)

target_include_directories(disassembler PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)
