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

add_executable(arm64_backward_slicing arm64_backward_slicing.cpp)

target_link_libraries(arm64_backward_slicing PRIVATE dragon::libdragon)

target_include_directories(arm64_backward_slicing PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)

add_executable(llvmir_lifting llvmir_lifting.cpp)

target_link_libraries(llvmir_lifting PRIVATE dragon::libdragon)

target_include_directories(llvmir_lifting PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)

if(DRAGON_USE_Z3)
    # Symbolic execution of lifted IGNIL: solving for inputs, proving MBA
    # identities, and resolving computed jumps to their real targets.
    add_executable(symbolic_execution symbolic_execution.cpp)

    target_link_libraries(symbolic_execution PRIVATE dragon::libdragon z3::libz3)

    target_include_directories(symbolic_execution PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )

    add_executable(z3_path_discovery z3_path_discovery.cpp)

    target_link_libraries(z3_path_discovery PRIVATE dragon::libdragon z3::libz3)

    target_include_directories(z3_path_discovery PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )
endif()

if(DRAGON_USE_UNICORN)
    # Prints every stage of tests/toy-vm/'s devirtualization pipeline (trace,
    # obfuscated IGNIL, raw and optimized LLVM IR) - see that directory for
    # why this needs Unicorn + orcjit/native, same as the semantic round-trip
    # tests.
    add_executable(toy_vm_devirtualization toy_vm_devirtualization.cpp)

    target_link_libraries(toy_vm_devirtualization PRIVATE dragon::libdragon)

    target_include_directories(toy_vm_devirtualization PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )

    llvm_map_components_to_libnames(TOY_VM_JIT_LIBS orcjit native)
    target_link_libraries(toy_vm_devirtualization PRIVATE
            PkgConfig::UNICORN ${TOY_VM_JIT_LIBS}
    )

    # Segments one captured trace into one episode per VM handler and lifts
    # each independently - see tests/toy-vm/ToyVMHarness.hpp for why this is
    # a meaningfully different (and more honest) technique than
    # toy_vm_devirtualization.cpp's whole-trace flattening.
    add_executable(toy_vm_handler_recovery toy_vm_handler_recovery.cpp)

    target_link_libraries(toy_vm_handler_recovery PRIVATE dragon::libdragon)

    target_include_directories(toy_vm_handler_recovery PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${LLVM_INCLUDE_DIRS}
    )

    target_link_libraries(toy_vm_handler_recovery PRIVATE
            PkgConfig::UNICORN ${TOY_VM_JIT_LIBS}
    )
endif()