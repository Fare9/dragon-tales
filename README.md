# dragon-tales

A binary analysis library for disassembling, assembling, and analyzing machine code across multiple architectures. Built on LLVM 21.

Features:
- Disassembly (linear sweep and recursive traversal) and assembly across six architectures
- CFG and function recovery
- **IGNIL** — a lightweight intermediate language for architecture-independent analysis
- **Backward slicer** — propagates values through IGNIL CFGs without crossing join nodes
- **FLIRT-compatible signature matching** — identify known functions from `.pat` signature databases (C++ only so far; not yet exposed through the C shim/Python API)
- C shim (`dragon_c.h`) and Python API (cffi) exposing the full feature set

## Requirements

- LLVM 21 (must be installed on the system)
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
    lifters/ignil/IGNIL.hpp          — IGNIL value types, opcodes, Block
    lifters/ignil/x86/x86IGNILRegs.hpp — x86/x86-64 register constants (offsets, widths, names)
    lifters/ignil/arm/armIGNILRegs.hpp — AArch64 register constants (offsets, widths, names)
    analysis/SliceExpr.hpp           — expression tree produced by the backward slicer
    analysis/BackwardSlicer.hpp      — backward slicer over IGNIL CFGs
    sigs/SignatureDB.hpp             — loaded collection of FLIRT .pat signatures
    sigs/SignatureMatcher.hpp        — matches signatures against raw bytes
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
#include <dragon/disassembler/Disassembler.hpp>

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
#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

// graph must be populated by Disassembler::disassemble(bytes, base_addr, graph)
// so IGNIL blocks exist
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

**ARM64 works the same way** — swap `Architecture::X86_64` for `Architecture::ARM64`
and `dragon::x86` / `dragon.x86` for `dragon::arm` / `dragon.arm`:

```cpp
#include <dragon/lifters/ignil/arm/armIGNILRegs.hpp>

auto expr = slicer.slice(dragon::arm::ZF, block_addr, graph);
// e.g. cmp w0, #5  ->  EQ(SUB(W0:i32, 0x5:i32), 0x0:i32)
```
