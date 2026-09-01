# C++ API

## Headers

```
include/dragon/Architecture.hpp
include/dragon/Configuration.hpp
include/dragon/disassembler/Disassembler.hpp
include/dragon/assembler/Assembler.hpp
include/dragon/disassembler/Graph.hpp
include/dragon/disassembler/Instruction.hpp
include/dragon/disassembler/BasicBlock.hpp
include/dragon/disassembler/Function.hpp
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
#include <dragon/disassembler/Disassembler.hpp>
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

Populate a `Graph` using the `Graph`-taking overload of `disassemble()` —
this runs the architecture-specific analysis pass that recovers block and
function boundaries, not just the flat instruction listing:

```cpp
#include <dragon/disassembler/Graph.hpp>

dragon::Configuration config;
dragon::Graph graph(dragon::Architecture::X86_64, config);

dragon::Disassembler dis(dragon::Architecture::X86_64, config);
dis.disassemble(bytes, 0x1000, graph);

// Instruction lookup by address
const dragon::Instruction* insn = graph.get(0x1000);
if (insn) std::cout << insn->disassembly() << "\n";

// Block view
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

> Building a `Graph` by decoding a flat listing and calling
> `graph.addInstruction()` per instruction compiles, but does **not** run
> block/function analysis — `bb.isValid()`/`fn.isValid()` will be `false`.
> Always use the `Graph`-overload of `disassemble()` shown above if you need
> `BasicBlock`/`Function`. See `docs/manual/03-disassembling-your-first-binary.md`
> for a longer walkthrough of the two modes.

---

## Assembler

```cpp
#include <dragon/assembler/Assembler.hpp>
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

---

## IGNIL

`Disassembler::disassemble(bytes, baseAddress, graph)` — the `Graph&`-taking
overload — populates both the CFG and the IGNIL blocks of the `Graph` passed
in. Each basic block has a corresponding `ignil::Block` that holds the
lifted ops for that block.

```cpp
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

dragon::Configuration config;
dragon::Disassembler dis(dragon::Architecture::X86_64, config);
dragon::Graph graph(dragon::Architecture::X86_64, config);

// nop; cmp rax, rbx; je 0x100a; ret
std::vector<uint8_t> code = { 0x90, 0x48, 0x39, 0xD8, 0x74, 0x03, 0xC3, 0xC3 };
dis.disassemble(code, /*baseAddress=*/0x1000, graph);

// Get the IGNIL block for a known address
if (auto* ib = graph.getIGNILBlock(0x1000)) {
    std::cout << dragon::ignil::toString(*ib);
    // IGNILBlock @ 0x1000
    //   t0:i64 = SUB(RAX:i64, RBX:i64)  ; @0x1001
    //   t1:i1 = ULT(RAX:i64, RBX:i64)  ; @0x1001
    //   CF:i8 = COPY(t1:i1)  ; @0x1001
    //   ...

    // Iterate individual ops
    for (const auto& op : ib->ops()) {
        std::cout << dragon::ignil::opcodeName(op.opcode) << "\n";
        if (op.dst)
            std::cout << "  dst: " << dragon::ignil::toString(*op.dst) << "\n";
        for (const auto& src : op.srcs)
            std::cout << "  src: " << dragon::ignil::toString(src) << "\n";
    }
}
```

### Trace lifting

`Disassembler::liftTrace` lifts an arbitrary linear sequence of
already-decoded instructions straight to IGNIL — e.g. a captured execution
trace (Unicorn/QBDI/Frida/Intel PT) that isn't claimed to form a single
valid basic block or a coherent CFG. It bypasses `disassemble`'s
CFG/function/block-validity checks entirely: each instruction is lifted
independently and the results concatenated in order, with no synthesized
fall-through terminator. Unlike `Graph::getIGNILBlock`'s borrowed pointer,
this returns a `Block` by value — the caller owns it outright.

```cpp
std::vector<dragon::Instruction> trace = /* e.g. addresses visited by a
                                             real emulator run, in order,
                                             not necessarily contiguous */;

dragon::ignil::Block block = dis.liftTrace(trace);
std::cout << dragon::ignil::toString(block);
```

Empty for RISC-V (no IGNIL lifter yet) or an empty trace.

### Peephole simplification

`ignil::simplifyBlock` (`<dragon/lifters/ignil/IGNILSimplify.hpp>`) runs a
block-local peephole simplifier to a fixed point: constant
folding/propagation, algebraic identity/annihilator elimination, no-op
width-conversion elimination, and dead-Temp elimination. It never crosses
into predecessor/successor blocks, and never removes a `Reg`-defining op
even if apparently unused within the block (a `Reg` may be live-out).

```cpp
#include <dragon/lifters/ignil/IGNILSimplify.hpp>

dragon::ignil::Block block = dis.liftTrace(trace);

// Stats-reporting overload
dragon::ignil::SimplifyStats stats;
dragon::ignil::Block simplified =
    dragon::ignil::simplifyBlock(block, stats, /*maxIterations=*/8);

std::cout << "folded=" << stats.constantsFolded
          << " identities=" << stats.identitiesApplied
          << " removed=" << stats.opsRemoved
          << " iterations=" << stats.iterations
          << " changed=" << stats.changed() << "\n";

// Or, if the stats aren't needed:
dragon::ignil::Block quiet = dragon::ignil::simplifyBlock(block);
```

### x86 register constants

`include/dragon/lifters/ignil/x86/x86IGNILRegs.hpp` provides named `ignil::Reg`
constants for every x86/x86-64 register in the `dragon::x86` namespace:

```cpp
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

namespace x86 = dragon::x86;

// Each constant is an ignil::Reg{offset, width, name}
ignil::Reg rax = x86::RAX;    // {0,  64, "RAX"}
ignil::Reg zf  = x86::ZF;     // {139, 8, "ZF"}
ignil::Reg ah  = x86::AH;     // {1,   8, "AH"}

// Offset constants and width aliases are also available
static_assert(x86::kOffZF == 139);
static_assert(x86::k64    ==  64);
```

Available groups:
- 64-bit: `RAX RCX RDX RBX RSP RBP RSI RDI R8..R15 RIP`
- 32-bit: `EAX ECX EDX EBX ESP EBP ESI EDI R8D..R15D`
- 16-bit: `AX CX DX BX SP BP SI DI R8W..R15W`
- 8-bit low: `AL CL DL BL SPL BPL SIL DIL R8B..R15B`
- 8-bit high: `AH CH DH BH`
- Flags (8-bit slots): `CF PF AF ZF SF OF DF`

### AArch64 register constants

`include/dragon/lifters/ignil/arm/armIGNILRegs.hpp` provides the same kind of
named `ignil::Reg` constants for AArch64, in the `dragon::arm` namespace:

```cpp
#include <dragon/lifters/ignil/arm/armIGNILRegs.hpp>

namespace arm = dragon::arm;

// Each constant is an ignil::Reg{offset, width, name}
ignil::Reg x0 = arm::X0;   // {0,   64, "X0"}
ignil::Reg zf = arm::ZF;   // {785,  8, "ZF"}
ignil::Reg w0 = arm::W0;   // {0,   32, "W0"}   -- 32-bit view of X0

// X29/X30 have both their architectural name and their ABI alias
static_assert(arm::X29.offset == arm::FP.offset); // frame pointer
static_assert(arm::X30.offset == arm::LR.offset); // link register

// Offset constants and width aliases are also available
static_assert(arm::kOffZF == 785);
static_assert(arm::k64    ==  64);
```

Available groups:
- 64-bit GPRs: `X0..X28 X29(FP) X30(LR) XZR SP PC`
- 32-bit views: `W0..W28 W29 W30 WZR WSP` (lower half of the matching X register)
- NEON/FP (128-bit slot, progressively narrower views): `V0..V31`/`Q0..Q31` (128-bit),
  `D0..D31` (64-bit), `S0..S31` (32-bit), `H0..H31` (16-bit), `B0..B31` (8-bit)
- NZCV flags (8-bit slots): `NF ZF CF VF`

---

## Backward slicer

`BackwardSlicer` propagates a value backward through an IGNIL CFG and returns
a `SliceExpr` expression tree. It stops at join nodes (blocks with more than
one predecessor) and at a configurable block depth.

`graph` must be populated via `Disassembler::disassemble(bytes, baseAddress, graph)`
so its IGNIL blocks exist. `blockAddr` must be the *start address of an IGNIL
block* (as returned by `graph.getIGNILBlock()`), not an arbitrary instruction
address inside it — passing a mid-block address returns `Undef`.

```cpp
#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

namespace x86 = dragon::x86;

dragon::BackwardSlicer slicer(/*maxBlocks=*/5);

// Slice ZF backward starting from the block at 0x1000 (nop; cmp; je; ret)
auto expr = slicer.slice(x86::ZF, /*blockAddr=*/0x1000, graph);
std::cout << dragon::slice::toString(expr) << "\n";
// EQ(SUB(RAX:i64, RBX:i64), 0x0:i64)
```

### ARM64 example

The same call works unchanged for AArch64 — swap the register namespace:

```cpp
#include <dragon/lifters/ignil/arm/armIGNILRegs.hpp>

namespace arm = dragon::arm;

// cmp w0, #5 ; ret   (block starts at 0x1000)
auto expr = slicer.slice(arm::ZF, /*blockAddr=*/0x1000, graph);
std::cout << dragon::slice::toString(expr) << "\n";
// EQ(SUB(W0:i32, 0x5:i32), 0x0:i32)
```

### Walking the expression tree

```cpp
#include <dragon/analysis/SliceExpr.hpp>

void print(const dragon::slice::Expr& e, int indent = 0) {
    std::string pad(indent * 2, ' ');
    std::visit(dragon::utils::overloaded{
        [&](const dragon::slice::Const& c) {
            std::cout << pad << "Const 0x" << std::hex << c.value
                      << ":i" << (int)c.width << "\n";
        },
        [&](const dragon::slice::SymReg& r) {
            std::cout << pad << "Reg " << r.reg.name
                      << ":i" << (int)r.reg.width << "\n";
        },
        [&](const dragon::slice::BinOp& b) {
            std::cout << pad << dragon::ignil::opcodeName(b.op) << "\n";
            print(*b.left,  indent + 1);
            print(*b.right, indent + 1);
        },
        [&](const dragon::slice::UnaryOp& u) {
            std::cout << pad << dragon::ignil::opcodeName(u.op) << "\n";
            print(*u.operand, indent + 1);
        },
        [&](const dragon::slice::Load& l) {
            std::cout << pad << "Load:i" << (int)l.width << "\n";
            print(*l.addr, indent + 1);
        },
        [&](const dragon::slice::Undef&) {
            std::cout << pad << "undef\n";
        },
    }, e.value);
}

print(expr);
// EQ
//   SUB
//     Reg RAX:i64
//     Reg RBX:i64
//   Const 0x0:i64
```

### SliceExpr node types

| Variant | Key fields | Meaning |
|---|---|---|
| `Const` | `value`, `width` | Folded constant |
| `SymReg` | `reg` (offset, width, name) | Unresolved register |
| `BinOp` | `op`, `left`, `right` | Binary operation |
| `UnaryOp` | `op`, `operand` | Unary operation |
| `Load` | `addr`, `width` | Memory read |
| `Undef` | — | Unresolvable value |

---

## LLVMLifter

`LLVMLifter` lifts a `Function` (and its IGNIL blocks) to LLVM IR. The result
is stored in an `llvm::Module` owned by the lifter. Call the optimization
passes to clean up the raw output before inspecting or emitting the IR.

`graph` must come from `Disassembler::disassemble(bytes, baseAddress, graph)` —
IGNIL blocks must be present.

```cpp
#include <dragon/lifters/llvm/LLVMLifter.hpp>

// push rbp; mov rbp, rsp; add rax, rbx; pop rbp; ret
std::vector<uint8_t> code = {
    0x55, 0x48, 0x89, 0xE5, 0x48, 0x01, 0xD8, 0x5D, 0xC3
};

dragon::Configuration cfg;
dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
dragon::Graph graph(dragon::Architecture::X86_64, cfg);
dis.disassemble(code, 0x1000, graph);

dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64, "my_module");

dragon::Function fn(&graph, 0x1000);
if (lifter.lift(fn, graph)) {
    std::cout << lifter.ir();   // raw IR

    lifter.runMem2Reg();
    lifter.runInstCombine();
    lifter.runGVN();
    lifter.runDCE();

    std::cout << lifter.ir();   // optimized IR
}
```

### Optimization passes

Run them in order for best results.

| Method | Effect |
|---|---|
| `runMem2Reg()` | Promotes allocas to SSA values, inserts φ-nodes |
| `runInstCombine()` | Folds sub-register mask/OR patterns |
| `runGVN()` | Eliminates redundant loads |
| `runDCE()` | Removes dead flag computations |

### Lifted function signature

Every lifted function has the signature:

```llvm
define void @sub_<addr>(ptr %mem, ptr %regs)
```

- `%mem` — flat memory image; loads and stores go through GEP offsets into this pointer.
- `%regs` — register file; registers are loaded from it on entry and written back on `ret`.

### Lifting a single block without a Graph

`liftBlock` is the `Graph`-free counterpart to `Disassembler::liftTrace` —
lift an already-flattened `ignil::Block` (e.g. the result of `liftTrace`,
optionally run through `simplifyBlock` first) straight to LLVM IR, with no
`Graph`/`Function` needed:

```cpp
dragon::ignil::Block block = dis.liftTrace(trace);

dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64);
if (lifter.liftBlock(block, "my_trace")) {
    lifter.runMem2Reg();
    lifter.runInstCombine();
    std::cout << lifter.ir();
}
```

Because a linear, already-resolved trace has no unresolved branch left to
encode (the block's op order *is* the path that was actually taken),
`JMP`/`CJMP` ops — which `liftTrace` still emits per-instruction, same as
normal lifting — are dropped rather than translated, instead of producing
`unreachable` (their fate under the `Graph`-based `lift()` when a target
isn't in the function). `CALL` is preserved as a real LLVM call. If the
block ends in `RET`, anything after it is ignored, matching how a real
`ret` would end execution; if it has no `RET`, a synthetic `ret void` (with
the usual register write-back) is appended so the function is always
well-formed. Returns `false` for an empty block.

After `runMem2Reg()` the alloca-based register representation is promoted to
SSA values. The `%regs` boundary loads/stores are the only register memory
traffic that remains.

---

## Z3 integration

Available when the library is built with `-DDRAGON_USE_Z3=ON`. The macro
`DRAGON_WITH_Z3` is defined in that case.

### SliceExprZ3 — lowering to Z3

`SliceExprZ3` translates a `SliceExpr` tree into a Z3 bitvector expression.
`SymReg` nodes become free bitvector constants; `Load` nodes become
unconstrained symbols keyed by their address expression. Reusing the same
`SliceExprZ3` instance across multiple `lower()` calls within one solver
session shares symbols, so constraints written against one call remain
consistent with the next.

```cpp
#ifdef DRAGON_WITH_Z3
#include <dragon/analysis/SliceExprZ3.hpp>

z3::context ctx;
dragon::slice::SliceExprZ3 lowerer{ctx};

z3::expr formula = lowerer.lower(expr);
std::cout << formula << "\n";   // (bvadd rax #x0000000000000004)

// Clear the symbol cache between independent solver sessions.
lowerer.reset();
#endif
```

### ValueEnumerator — enumerating satisfying values

`enumeratedValues` asks Z3 for all distinct 64-bit concrete values the
expression can take, adding a blocking clause after each model. `solveUnique`
is a convenience wrapper that returns a value only when exactly one model
exists.

```cpp
#ifdef DRAGON_WITH_Z3
#include <dragon/analysis/ValueEnumerator.hpp>

z3::context ctx;

// Enumerate all values (up to 256 by default)
auto values = dragon::slice::enumeratedValues(expr, ctx);
for (uint64_t v : values)
    std::cout << std::hex << v << "\n";

// Limit the search and add extra constraints
z3::expr rax = ctx.bv_const("RAX", 64);
dragon::slice::EnumeratorConfig cfg{16};   // stop after 16 results
auto bounded = dragon::slice::enumeratedValues(
    expr, ctx,
    {z3::ult(rax, ctx.bv_val(static_cast<uint64_t>(8), 64u))},
    cfg);

// Solve for a unique concrete value
auto unique = dragon::slice::solveUnique(expr, ctx);
if (unique)
    std::cout << "unique target: 0x" << std::hex << *unique << "\n";
else
    std::cout << "not uniquely determined\n";
#endif
```

**API summary**

| Symbol | Description |
|---|---|
| `SliceExprZ3(ctx)` | Construct with a Z3 context |
| `lower(expr)` → `z3::expr` | Lower a SliceExpr tree to a bitvector formula |
| `reset()` | Clear the symbol cache between solver sessions |
| `enumeratedValues(expr, ctx, constraints, cfg)` → `vector<uint64_t>` | Enumerate all satisfying 64-bit values |
| `solveUnique(expr, ctx, constraints)` → `optional<uint64_t>` | Return value only when exactly one model exists |
| `EnumeratorConfig::maxValues` | Maximum solutions to collect (default 256) |
| `EnumeratorConfig::sorted` | Sort results ascending (default true) |

### SymbolicExecutor — running IGNIL over Z3

`SymbolicExecutor` interprets IGNIL ops against a `SymbolicState`, producing a
Z3 bitvector expression for every value the block computes. Registers and
memory can be seeded concretely, symbolically, or left alone — an untouched
location reads back as a fresh free variable, so a block always executes even
when its inputs are unknown.

It is architecture-agnostic: the executor only ever sees `ignil::Reg` offsets,
so an x86 block and an AArch64 block run through the same code.

```cpp
#ifdef DRAGON_WITH_Z3
#include <dragon/lifters/ignil/SymbolicExecutor.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

namespace ignil = dragon::ignil;
namespace x86   = dragon::x86;

ignil::SymbolicExecutor exec;
exec.setRegister(x86::RAX, 0);
z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
z3::expr b = exec.symbolizeRegister(x86::RSI, "b");

ignil::RunResult r = exec.run(*graph.getIGNILBlock(0x1000));

// Ask about the value, not the syntax - the flag-setting ops the lifter
// emits alongside the arithmetic do not get in the way.
z3::expr rax = exec.readRegister(x86::RAX);
exec.proves(rax == a + b);          // std::optional<bool>
#endif
```

State persists across `run()` calls, so walking a path is a sequence of runs.

#### Solving — recovering an input

Symbolise a buffer, run the block that checks it, then ask for an assignment
that makes the check pass. A symbolic region of N bytes creates N eight-bit
variables named `<region>[0]`..`<region>[N-1]`, which `Solution::bytes()`
reads back in address order.

```cpp
ignil::SymbolicExecutor exec;
exec.setRegister(x86::RDI, 0x4000);
exec.symbolizeMemory(0x4000, 8, "flag");
exec.run(block);

auto solution = exec.solve(exec.readRegister(x86::RAX) == exec.bv(target, 64));
if (solution) {
    std::vector<std::uint8_t> flag = solution->bytes("flag");
    std::uint64_t             n    = solution->value("some_symbol").value_or(0);
}
```

`solve()` returns `std::nullopt` when no assignment exists — an unreachable
goal is reported, never guessed at. Constrain the search with `assume()`
before running, e.g. to keep an input inside a charset:

```cpp
for (int i = 0; i < 4; ++i) {
    z3::expr byte = exec.readMemory(0x5000 + i, 8);
    exec.assume(z3::uge(byte, exec.bv(0x21, 8)));
    exec.assume(z3::ule(byte, exec.bv(0x7E, 8)));
}
```

#### Control flow — resolving computed jumps

`RunResult::successors` names the concrete addresses control flow can reach,
each with the guard under which it is reached. A `CJMP` whose condition is
provably constant under the path constraints collapses to a single successor —
that is how an opaque predicate disappears, with no pattern matching involved.
A computed `JMP` has its target expression enumerated, which is what recovers a
jump table or a VM dispatch:

```cpp
exec.mapMemory(0x6000, handlerTable);
z3::expr opcode = exec.symbolizeMemory(0x7000, 4, "vm_opcode");
exec.assume(z3::ult(opcode, exec.bv(3, 32)));   // the VM's range check

ignil::RunResult r = exec.run(dispatcher);
for (const ignil::Successor &s : r.successors)
    std::cout << "handler at 0x" << std::hex << s.address << "\n";
```

`RunResult::complete()` is the honesty check: it is true only when the run
succeeded *and* every address and branch target resolved exactly, so the
successor list really is the complete set of reachable addresses. Otherwise
`warnings` says what had to be left open — an address with more candidates
than the budget allows, or a solver timeout. Nothing is invented to fill the
gap.

#### Modelling notes

- **Registers and memory are byte-granular.** That is what makes sub-register
  aliasing come out right with no per-architecture table: `EAX` writes bytes
  0–3, `AH` writes byte 1, and a later `RAX` read concatenates bytes 0–7
  whatever mix of widths produced them.
- **A 32-bit write clears the upper half** of a 64-bit register, matching
  x86-64 and AArch64. Turn it off with
  `SymbolicOptions::zeroExtendOn32BitWrite` for a target where a 32-bit write
  merges.
- **A symbolic address is concretized**, not modelled with the theory of
  arrays: the executor enumerates the address's satisfying values (bounded by
  `maxAddressSolutions`) and folds them into a select chain, or a conditional
  update for a store. Past the bound the access is reported unresolved.
- **Division follows Z3's total semantics** (`x/0` is all-ones unsigned).
  IGNIL leaves division by zero unspecified, so constrain the divisor if it
  matters. `SMOD` is `srem`, matching the `%` `IGNILSimplify` folds with.
- **`CALL` does not model the callee.** Its target is reported as a successor
  and nothing else happens.
- **Blocks stop at the first terminator.** Set
  `SymbolicOptions::stopAtTerminator = false` to run a *trace* from
  `liftTrace()`, whose interior terminators must be stepped over.

#### Options

```cpp
ignil::SymbolicOptions opts;
opts.solverTimeoutMs        = 5000;  // per query
opts.maxAddressSolutions    = 64;    // candidates for a symbolic LOAD/STORE
opts.maxBranchTargets       = 256;   // candidates for a computed jump
opts.zeroExtendOn32BitWrite = true;
opts.symbolizeUnmappedReads = true;  // false -> untouched locations read zero
opts.stopAtTerminator       = true;  // false -> trace mode

ignil::SymbolicExecutor exec{opts};
// Or share a context with other Z3 work:
z3::context ctx;
ignil::SymbolicExecutor shared{ctx, opts};
```

**API summary**

| Symbol | Description |
|---|---|
| `SymbolicExecutor(opts)` / `(ctx, opts)` | Own a Z3 context, or borrow one |
| `setRegister(reg, value)` | Seed a register concretely |
| `symbolizeRegister(reg, name)` → `z3::expr` | Bind a register to a fresh variable |
| `mapMemory(addr, bytes)` | Map a concrete blob |
| `symbolizeMemory(addr, size, name)` → `z3::expr` | Fill a region with per-byte variables |
| `assume(cond)` | Constrain the rest of the run |
| `run(block)` → `RunResult` | Execute a block; state persists |
| `step(op, result)` → `ExecStatus` | Execute one op |
| `readRegister(reg)` / `readMemory(addr, width)` → `z3::expr` | What a location now holds |
| `bv(value, width)` → `z3::expr` | A concrete bitvector in this context |
| `proves(cond)` → `optional<bool>` | Does it hold on every model? |
| `isSatisfiable(cond)` → `optional<bool>` | Can it hold at all? |
| `evaluate(expr)` → `optional<uint64_t>` | Value, only if it is forced to exactly one |
| `enumerate(expr, max, &exhaustive)` → `vector<uint64_t>` | Distinct satisfying values |
| `solve(goal)` → `optional<Solution>` | A satisfying assignment, or nullopt |
| `state()` / `setState(s)` / `reset()` | Snapshot, restore, or clear the machine |
| `RunResult::successors` | Reachable addresses, each with its guard |
| `RunResult::complete()` | True only if that list is exhaustive |
| `RunResult::warnings` | What had to be left unresolved |
| `Solution::value(name)` / `bytes(region)` | Read the model back |

The `symbolic_execution` example (`examples/cpp/symbolic_execution.cpp`) runs
all of the above against real lifted x86-64.

---

## SliceExprMatch

`SliceExprMatch.hpp` provides LLVM PatternMatch.h-style composable structural
matchers for `SliceExpr` trees. Matchers capture values as a side effect only
on a **full** match — a partial match that ultimately fails leaves all
captured variables untouched.

```cpp
#include <dragon/analysis/SliceExprMatch.hpp>

using namespace dragon::slice::match;

uint64_t base, scale;
dragon::ignil::Reg idx;

// Recognise LOAD( ADD(Const{base}, MUL(SymReg{idx}, Const{scale})) )
// in any operand order (commutative ADD and MUL)
if (match(expr,
          m_Load(m_AddAnyOrder(m_Const(base),
                               m_MulAnyOrder(m_SymReg(idx),
                                             m_Const(scale)))))) {
    // base, idx, scale are now bound
}
```

### Leaf matchers

| Matcher | Captures | Matches |
|---|---|---|
| `m_Any()` | — | Any node |
| `m_Const()` | — | Any `Const` node |
| `m_Const(value)` | `uint64_t&` | Any `Const`; binds value |
| `m_Const(value, width)` | `uint64_t&`, `Width&` | Any `Const`; binds both fields |
| `m_SymReg()` | — | Any `SymReg` node |
| `m_SymReg(reg)` | `ignil::Reg&` | Any `SymReg`; binds the register descriptor |

### Structural matchers

| Matcher | Matches |
|---|---|
| `m_Load(addr_matcher)` | `Load` node whose address matches `addr_matcher` |
| `m_Load(addr_matcher, width)` | Same; also captures the load width |
| `m_BinOp(opcode, lm, rm)` | `BinOp` with exact opcode; left/right matched by `lm`/`rm` |
| `m_Add(lm, rm)` | `ADD` — left then right, strict order |
| `m_Sub(lm, rm)` | `SUB` |
| `m_Mul(lm, rm)` | `MUL` — strict order |
| `m_And(lm, rm)` | `AND` |
| `m_Or(lm, rm)` | `OR` |
| `m_Shl(lm, rm)` | `SHL` |
| `m_UnaryOp(opcode, om)` | `UnaryOp` with exact opcode |
| `m_ZExt(om)` | `ZEXT` |
| `m_SExt(om)` | `SEXT` |
| `m_Trunc(om)` | `TRUNC` |
| `m_Neg(om)` | `NEG` |
| `m_Not(om)` | `NOT` |

### Commutative combinators

`BinOp` matchers are **not** commutative by default. Use `m_AnyOrder` to try
both operand orderings before failing:

| Matcher | Equivalent to |
|---|---|
| `m_AnyOrder(opcode, lm, rm)` | Try `(lm, rm)` then `(rm, lm)` |
| `m_AddAnyOrder(lm, rm)` | Commutative `ADD` |
| `m_MulAnyOrder(lm, rm)` | Commutative `MUL` |

---

## SwitchDetector

`SwitchDetector.hpp` identifies jump-table indirect branches from a
backward-sliced expression and reads the table entries from the binary image.

Recognition is two-phase:

1. **Pattern match** — check whether the slice expression has the form
   `LOAD( ADD(Const{base}, MUL(SymReg{idx}, Const{scale})) )` with
   `scale` equal to 4 or 8. Both ADD and MUL are matched commutatively.
2. **Bounds extraction** — walk the predecessor blocks of the switch block
   looking for a unique CJMP whose sliced condition is a comparison of the
   index register against a constant (`UGE`, `UGT`, or `NOT(ULT)`). The case
   count is stored in `SwitchCandidate::numCases`.

```cpp
#include <dragon/analysis/SwitchDetector.hpp>

// graph must come from Disassembler::analyze()
dragon::BackwardSlicer slicer{5};
dragon::slice::Expr jumpTarget =
    slicer.slice(ignil::Value{reg}, blockAddr, graph);

auto sw = dragon::slice::detectPossibleSwitch(jumpTarget, blockAddr, graph);

if (sw) {
    std::cout << "table @ 0x" << std::hex << sw->tableBase
              << "  entrySize=" << sw->entrySize
              << "  cases=" << sw->numCases << "\n";

    if (sw->numCases > 0) {
        // Read the actual target addresses from the binary image
        auto targets = dragon::slice::readTableTargets(*sw, bytes, baseAddr);
        for (uint64_t t : targets)
            std::cout << "  case -> 0x" << std::hex << t << "\n";
    }
}
```

**`SwitchCandidate` fields**

| Field | Type | Description |
|---|---|---|
| `tableBase` | `uint64_t` | Base address of the jump table in the binary image |
| `entrySize` | `uint32_t` | Bytes per entry: 4 (32-bit) or 8 (64-bit) |
| `numCases` | `uint32_t` | Number of table entries; 0 when bounds were not found |
| `indexReg` | `ignil::Reg` | Register used as the table index |

**API summary**

| Function | Description |
|---|---|
| `detectPossibleSwitch(expr, blockAddr, graph)` → `optional<SwitchCandidate>` | Returns nullopt when the expression does not match the jump-table pattern |
| `readTableTargets(sw, bytes, baseAddr)` → `vector<uint64_t>` | Reads `numCases` entries from the byte buffer; returns empty when out of range or `numCases == 0` |
