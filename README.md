# dragon-tales

A binary analysis library for disassembling, assembling, and analyzing machine code across multiple architectures. Built on LLVM 18.

Features:
- Disassembly (linear sweep and recursive traversal) and assembly across six architectures
- CFG and function recovery
- **IGNIL** — a lightweight intermediate language for architecture-independent analysis
- **Backward slicer** — propagates values through IGNIL CFGs without crossing join nodes
- C shim (`dragon_c.h`) and Python API (cffi) exposing the full feature set

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
  dragon/
    IGNIL.hpp           — IGNIL value types, opcodes, Block
    x86IGNILRegs.hpp    — x86/x86-64 register constants (offsets, widths, names)
    SliceExpr.hpp       — expression tree produced by the backward slicer
    BackwardSlicer.hpp  — backward slicer over IGNIL CFGs
lib/            C++ implementation
c_shim/         C API (dragon_c.h + libdragon_c.so)
python/         Python API (cffi, wraps the C shim)
tests/          Catch2 test suite
docs/           API documentation and examples
```

## Documentation

Full examples for every API layer are in [`docs/`](docs/):

- [`docs/cpp_api.md`](docs/cpp_api.md) — C++ API
- [`docs/c_api.md`](docs/c_api.md) — C shim API
- [`docs/python_api.md`](docs/python_api.md) — Python API
- [`IGNIL.md`](IGNIL.md) — IGNIL design and implementation guide

## Quick start

**C++**

```cpp
#include <dragon/Disassembler.hpp>

dragon::Configuration config;
dragon::Disassembler dis(dragon::Architecture::X86_64, config);

auto instructions = dis.disassemble(bytes, base_address);
for (const auto& insn : instructions)
    std::cout << std::hex << insn.address() << "  " << insn.disassembly() << "\n";
```

**Python**

```python
import sys
sys.path.insert(0, 'python')
import dragon

cfg = dragon.Configuration()
dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)
graph = dis.disassemble(b'\x90\xC3', base_address=0x1000)
for insn in graph.instructions():
    print(hex(insn.address), insn.disassembly)
```

**Backward slicing (C++)**

```cpp
#include <dragon/BackwardSlicer.hpp>
#include <dragon/x86IGNILRegs.hpp>

// graph must come from dis.analyze() to have IGNIL blocks populated
dragon::BackwardSlicer slicer;
auto expr = slicer.slice(dragon::x86::ZF, block_addr, graph);
std::cout << dragon::slice::toString(expr) << "\n";
// e.g. EQ(SUB(RAX:i64, RBX:i64), 0x0:i64)
```

**Backward slicing (Python)**

```python
with dragon.BackwardSlicer() as slicer:
    with slicer.slice_reg(graph, block_addr, *dragon.x86.ZF) as expr:
        print(expr.to_string())   # EQ(SUB(RAX:i64, RBX:i64), 0x0:i64)
```
