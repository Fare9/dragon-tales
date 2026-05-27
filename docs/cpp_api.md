# C++ API

## Headers

```
include/dragon/Architecture.hpp
include/dragon/Configuration.hpp
include/dragon/Disassembler.hpp
include/dragon/Assembler.hpp
include/dragon/Graph.hpp
include/dragon/Instruction.hpp
include/dragon/BasicBlock.hpp
include/dragon/Function.hpp
```

Link against `libdragon`.

---

## Configuration

`Configuration` groups all settings shared by the disassembler and assembler:
syntax, disassembly strategy, and the LLVM target knobs `cpu` and `features`.

```cpp
#include <dragon/Configuration.hpp>

dragon::Configuration config;
config.syntax               = dragon::Syntax::INTEL;                    // default
config.disassemblyStrategy  = dragon::DisassemblyStrategy::LINEAR_SWEEP; // default
// config.cpu and config.features default to "" → LLVM "generic" baseline
```

### Targeting a specific CPU or ISA revision

`cpu` selects a micro-architecture by name; `features` applies fine-grained
feature flags on top. Both are forwarded directly to LLVM's `MCSubtargetInfo`.

```cpp
// ARM32 — enable BX/BLX (requires ARMv4T, not present in the generic baseline)
dragon::Configuration arm32v5;
arm32v5.cpu = "arm926ej-s";   // ARMv5TEJ

// ARM32 — target ARMv7-A with NEON via feature flags instead of a CPU name
dragon::Configuration arm32v7;
arm32v7.features = "+v7,+neon";

// ARM64 — no special setup needed; the generic AArch64 baseline is complete
dragon::Configuration arm64;  // cpu and features left empty

// x86-64 — enable AVX-512 for a specific microarchitecture
dragon::Configuration x86avx512;
x86avx512.cpu = "skylake-avx512";

// RISC-V 64 — enable multiply, atomics and compressed extensions
dragon::Configuration rv64mac;
rv64mac.features = "+m,+a,+c";
```

**cpu examples by architecture**

| Architecture | CPU string | Enables |
|---|---|---|
| ARM32 | `"arm926ej-s"` | ARMv5TEJ — BX, BLX |
| ARM32 | `"cortex-a8"` | ARMv7-A, Thumb2, NEON, VFPv3 |
| ARM64 | `"cortex-a53"` | AArch64 baseline |
| ARM64 | `"apple-m1"` | AArch64 + Apple extensions |
| x86-64 | `"skylake"` | AVX2, BMI2, AES |
| x86-64 | `"znver3"` | AMD Zen 3 |
| RISC-V 64 | `"sifive-u74"` | RV64GC |

**features examples**

| String | Meaning |
|---|---|
| `"+v4t"` | ARM32: add BX (ARMv4T) |
| `"+v7,+neon"` | ARM32: ARMv7 + NEON |
| `"+sve"` | ARM64: Scalable Vector Extension |
| `"+avx2"` | x86: AVX2 |
| `"+m,+a,+c"` | RISC-V: multiply, atomics, compressed |
| `"+f,+d"` | RISC-V: single- and double-precision FP |

---

## Disassembler

### Flat listing

`Disassembler::disassemble` returns a `std::vector<Instruction>` in address order.

```cpp
#include <dragon/Disassembler.hpp>
#include <iostream>

// x86-64 bytes: nop; ret
std::vector<uint8_t> bytes = { 0x90, 0xC3 };

dragon::Configuration config;
dragon::Disassembler dis(dragon::Architecture::X86_64, config);

if (!dis.isValid()) { /* unsupported arch / LLVM init failure */ }

auto instrs = dis.disassemble(bytes, /*baseAddress=*/0x1000);

for (const auto& insn : instrs) {
    std::cout << std::hex << insn.address()
              << "  " << insn.disassembly()
              << "  [" << insn.bytes().size() << " bytes]\n";
}
// 0x1000  nop  [1 bytes]
// 0x1001  ret  [1 bytes]
```

### Instruction flags

```cpp
for (const auto& insn : instrs) {
    if (insn.isCall())     std::cout << "call  -> ";
    if (insn.isJump())     std::cout << "jump  -> ";
    if (insn.isReturn())   std::cout << "ret\n";
    if (insn.isConditional()) std::cout << "(conditional)\n";

    for (uint64_t target : insn.branches())
        std::cout << "  branch -> 0x" << std::hex << target << "\n";
    for (uint64_t target : insn.callees())
        std::cout << "  callee -> 0x" << std::hex << target << "\n";
}
```

### Recursive traversal

```cpp
dragon::Configuration config;
config.disassemblyStrategy = dragon::DisassemblyStrategy::RECURSIVE_TRAVERSAL;

dragon::Disassembler dis(dragon::Architecture::X86_64, config);
auto instrs = dis.disassemble(bytes, 0x1000);
```

---

## Graph, BasicBlock and Function

Populate a `Graph` from the flat listing, then query blocks and functions.

```cpp
#include <dragon/Graph.hpp>

dragon::Configuration config;
dragon::Graph graph(dragon::Architecture::X86_64, config);

dragon::Disassembler dis(dragon::Architecture::X86_64, config);
for (auto& insn : dis.disassemble(bytes, 0x1000))
    graph.insert(std::move(insn));

// Instruction lookup by address
const dragon::Instruction* insn = graph.get(0x1000);
if (insn) std::cout << insn->disassembly() << "\n";

// Block view (requires blocks_q to be populated by the disassembler)
dragon::BasicBlock bb(&graph, 0x1000);
if (bb.isValid()) {
    std::cout << "block size: " << bb.size() << " instructions\n";
    for (const dragon::Instruction* i : bb.instructions())
        std::cout << "  " << i->disassembly() << "\n";
    for (uint64_t succ : bb.successors())
        std::cout << "  successor -> 0x" << std::hex << succ << "\n";
}

// Function view
dragon::Function fn(&graph, 0x1000);
if (fn.isValid()) {
    std::cout << "function blocks:       " << fn.numBlocks() << "\n";
    std::cout << "function instructions: " << fn.numInstructions() << "\n";
    std::cout << "cyclomatic complexity: " << fn.cyclomaticComplexity() << "\n";
    for (const auto& [addr, block] : fn.blocks())
        std::cout << "  block @ 0x" << std::hex << addr << "\n";
}
```

---

## Assembler

```cpp
#include <dragon/Assembler.hpp>
#include <iostream>

dragon::Configuration config;                // INTEL syntax by default
dragon::Assembler asm_(dragon::Architecture::X86_64, config);

if (!asm_.isValid()) { /* LLVM init failure */ }

auto bytes = asm_.assemble("nop\nret\n");
if (bytes.empty()) { /* assembly failed */ }

for (uint8_t b : bytes)
    std::cout << std::hex << static_cast<int>(b) << " ";
// 90 c3
```

### AT&T syntax

```cpp
dragon::Configuration config;
config.syntax = dragon::Syntax::ATT;

dragon::Assembler asm_(dragon::Architecture::X86_64, config);
auto bytes = asm_.assemble("nop\nret\n");
```
