# dragon-tales

A binary analysis library for disassembling, assembling, and analyzing machine code across multiple architectures. Built on LLVM 21.

Features:
- Disassembly (linear sweep and recursive traversal) and assembly across six architectures
- CFG and function recovery
- **IGNIL** — a lightweight intermediate language for architecture-independent analysis
- **IGNIL peephole simplifier** — block-local constant folding, algebraic identities, dead-Temp elimination
- **Backward slicer** — propagates values through IGNIL CFGs without crossing join nodes
- **LLVMLifter** — lifts a recovered `Function`, or a single flattened IGNIL block (no CFG needed), to LLVM IR, with an optimization pipeline (mem2reg/instcombine/GVN/DCE) on top
- **Z3 integration** — lower a sliced expression to a Z3 bitvector formula, enumerate the concrete values it can take, or solve for a unique one
- **Symbolic execution** — run an IGNIL block over Z3 bitvectors with registers and memory seeded concretely or symbolically: solve for an input that makes a check pass, prove what an obfuscated expression really computes, and resolve a computed jump (jump table, VM dispatch) to the concrete addresses it can reach
- **SwitchDetector** — recognize a jump-table indirect-branch pattern from a sliced jump target and read its concrete case targets
- **FLIRT-compatible signature matching** — identify known functions from `.pat` signature databases
- **Trace lifting** — lift an arbitrary linear instruction sequence (e.g. a captured execution trace) straight to IGNIL, bypassing CFG/function/block-validity analysis entirely
- C shim (`dragon_c.h`) and Python API (cffi) covering most of the feature set above (a few C++-only corners — pattern-matching DSL templates, architecture-specific instruction classification helpers, and similar — are documented as such in `docs/cpp_api.md`)

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
    lifters/ignil/IGNILSimplify.hpp  — peephole simplifier (constant folding, identities, dead-Temp elimination)
    lifters/ignil/SymbolicExecutor.hpp — symbolic execution of IGNIL over Z3 (solving, branch/jump-target recovery)
    lifters/ignil/x86/x86IGNILRegs.hpp — x86/x86-64 register constants (offsets, widths, names)
    lifters/ignil/arm/armIGNILRegs.hpp — AArch64 register constants (offsets, widths, names)
    lifters/llvm/LLVMLifter.hpp      — lifts a Function or a single IGNIL block to LLVM IR
    analysis/SliceExpr.hpp           — expression tree produced by the backward slicer
    analysis/SliceExprMatch.hpp      — template-based structural pattern matcher over SliceExpr trees
    analysis/SliceExprZ3.hpp         — lowers a SliceExpr to a Z3 bitvector expression
    analysis/ValueEnumerator.hpp     — enumerate or uniquely solve a SliceExpr's possible values via Z3
    analysis/BackwardSlicer.hpp      — backward slicer over IGNIL CFGs
    analysis/SwitchDetector.hpp      — recognizes jump-table dispatch patterns and reads their targets
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

A hands-on walkthrough, organized by task rather than by class, is in
[`docs/manual/`](docs/manual/index.md) — from raw bytes through IGNIL and LLVM
IR to [symbolic execution](docs/manual/08-symbolic-execution.md).

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
