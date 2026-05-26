# dragon-tales

A binary analysis library for disassembling and assembling machine code across multiple architectures. Built on LLVM 18.

## Requirements

- LLVM 18 (must be installed on the system)
- CMake 3.20 or later
- A C++20 compiler
- Python 3 with cffi (optional, for the Python API)

## Supported architectures

- x86 (32-bit and 64-bit)
- ARM (32-bit and 64-bit)
- RISC-V (32-bit and 64-bit)

## Building

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

To run the tests:

```sh
ctest --output-on-failure
```

## Structure

```
include/        C++ public headers
lib/            C++ implementation
c_shim/         C API (dragon_c.h + libdragon_c.so)
python/         Python API (cffi, wraps the C shim)
tests/          Catch2 test suite
```

## C++ usage

```cpp
#include <dragon/Disassembler.hpp>
#include <dragon/Graph.hpp>

dragon::Configuration config;
dragon::Disassembler dis(dragon::Architecture::X86_64, config);

auto instructions = dis.disassemble(bytes, base_address);
for (const auto& insn : instructions)
    std::cout << std::hex << insn.address() << "  " << insn.disassembly() << "\n";
```

## Python usage

```python
import sys
sys.path.insert(0, 'python')
import dragon

with dragon.disassemble(b'\x90\xC3', base_address=0x1000) as g:
    for insn in g.instructions():
        print(hex(insn.address), insn.mnemonic, insn.bytes.hex())
```
