add_executable(ignil_lifting ignil_lifting.cpp)

target_link_libraries(ignil_lifting PRIVATE dragon::libdragon)

target_include_directories(ignil_lifting PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib
        ${LLVM_INCLUDE_DIRS}
)

add_executable(backward_slicing backward_slicing.cpp)

target_link_libraries(backward_slicing PRIVATE dragon::libdragon)

target_include_directories(backward_slicing PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)

add_executable(llvmir_lifting llvmir_lifting.cpp)

target_link_libraries(llvmir_lifting PRIVATE dragon::libdragon)

target_include_directories(llvmir_lifting PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)