add_executable(disassembler disassembler.cpp)

target_link_libraries(disassembler PRIVATE dragon::libdragon)

target_include_directories(disassembler PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)

add_executable(opcode_audit opcode_audit.cpp)

target_link_libraries(opcode_audit PRIVATE dragon::libdragon)

target_include_directories(opcode_audit PRIVATE
        ${LLVM_INCLUDE_DIRS}
)
