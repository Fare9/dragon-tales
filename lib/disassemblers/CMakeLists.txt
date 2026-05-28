target_sources(libdragon
        PRIVATE
        LLVMTarget.cpp
        Disassembler.cpp
)

add_subdirectory(x86)