add_executable(ignil_lifting ignil_lifting.cpp)

target_link_libraries(ignil_lifting PRIVATE dragon::libdragon)

target_include_directories(ignil_lifting PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib
        ${LLVM_INCLUDE_DIRS}
)
