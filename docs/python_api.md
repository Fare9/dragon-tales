# Python API

The Python API wraps the C shim via [cffi](https://cffi.readthedocs.io/).
Build the C shim first (`libdragon_c.so` must exist in `build/c_shim/`),
then import the module:

```python
import sys
sys.path.insert(0, 'python')
import dragon
```

All three main objects (`Configuration`, `Disassembler`, `Assembler`) support the
context-manager protocol for deterministic cleanup.

---

## Configuration

```python
cfg = dragon.Configuration(
    syntax   = dragon.Syntax.INTEL,           # default
    strategy = dragon.Strategy.LINEAR_SWEEP,  # default
    cpu      = '',                            # default — LLVM "generic" baseline
    features = '',                            # default — no extra features
)

print(cfg)          # <Configuration syntax=0 strategy=0>
print(cfg.syntax)   # 0  (INTEL)
print(cfg.strategy) # 0  (LINEAR_SWEEP)
print(cfg.cpu)      # ''
print(cfg.features) # ''
```

If you omit `config` when constructing a `Disassembler` or `Assembler`,
a default `Configuration` is created automatically.

### Targeting a specific CPU or ISA revision

`cpu` and `features` are forwarded to LLVM's `MCSubtargetInfo`. An empty
string uses the "generic" baseline for the target triple, which may omit
optional ISA extensions. Both are readable and writable properties.

```python
# ARM32 — enable BX/BLX (ARMv4T, absent from the generic ARM baseline)
cfg = dragon.Configuration(cpu="arm926ej-s")
dis = dragon.Disassembler(dragon.Architecture.ARM32, cfg)

# ARM32 — same effect using a feature flag instead of a CPU name
cfg = dragon.Configuration(features="+v4t")
dis = dragon.Disassembler(dragon.Architecture.ARM32, cfg)

# ARM32 — ARMv7-A with NEON
cfg = dragon.Configuration(features="+v7,+neon")

# x86-64 — Skylake micro-architecture
cfg = dragon.Configuration(cpu="skylake")

# RISC-V 64 — multiply, atomics and compressed extensions
cfg = dragon.Configuration(features="+m,+a,+c")

# Properties are writable after construction
cfg = dragon.Configuration()
cfg.cpu      = "cortex-a8"
cfg.features = "+neon"
```

**cpu examples by architecture**

| Architecture | `cpu` value | Enables |
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

### Basic usage

```python
code = bytes([0x90, 0xC3])   # nop; ret

cfg = dragon.Configuration(syntax=dragon.Syntax.INTEL)
dis = dragon.Disassembler(arch=dragon.Architecture.X86_64, config=cfg)

graph = dis.disassemble(code, base_address=0x1000)

for insn in graph.instructions():
    print(f"0x{insn.address:x}  {insn.disassembly}")
# 0x1000  nop
# 0x1001  ret
```

### Using the context manager

```python
cfg = dragon.Configuration()
with dragon.Disassembler(dragon.Architecture.X86_64, cfg) as dis:
    with dis.disassemble(code, 0x1000) as graph:
        for insn in graph.instructions():
            print(hex(insn.address), insn.mnemonic, insn.operands)
```

### Instruction properties

```python
for insn in graph.instructions():
    print(insn.address)          # int
    print(insn.size)             # int (bytes)
    print(insn.mnemonic)         # str
    print(insn.operands)         # str
    print(insn.disassembly)      # mnemonic + operands
    print(insn.bytes.hex())      # raw encoding, e.g. "90"
    print(insn.pattern)          # wildcard pattern string
    print(insn.chromosome_mask)  # bytes

    print(insn.is_call)
    print(insn.is_jump)
    print(insn.is_return)
    print(insn.is_conditional)
    print(insn.is_block_start)
    print(insn.is_function_start)
    print(insn.edges)

    print(insn.branches)   # list of target addresses
    print(insn.callees)    # list of callee addresses
```

### Recursive traversal

```python
cfg = dragon.Configuration(strategy=dragon.Strategy.RECURSIVE_TRAVERSAL)
dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)
graph = dis.disassemble(code, 0x1000)
```

### Trace lifting

`Disassembler.lift_trace()` lifts an arbitrary linear sequence of
already-decoded instructions straight to IGNIL — e.g. a captured execution
trace (Unicorn/QBDI/Frida/Intel PT) that isn't claimed to form a single
valid basic block or a coherent CFG. It bypasses `disassemble()`'s
CFG/function/block-validity analysis entirely: each instruction is lifted
independently and the results concatenated in order, with no synthesized
fall-through terminator.

```python
graph = dis.disassemble(code, 0x1000)
insns = graph.instructions()   # or hand-pick/reorder — need not be contiguous

with dis.lift_trace(insns) as block:
    print(block.to_string())
```

The returned `IGNILBlock` is **owned** — unlike `Graph.get_ignil_block()`'s
borrowed one — and gets destroyed on `__del__`/context-manager exit. It's
still the same `IGNILBlock` class either way; only the `owned` flag differs
internally.

---

## Graph: blocks and functions

```python
# Look up a single instruction by address
insn = graph.get_instruction(0x1000)
if insn:
    print(insn.disassembly)

# Block addresses that were identified
for addr in graph.block_addresses():
    print(f"block @ {hex(addr)}")

# Inspect a single block
bb = graph.get_block(0x1000)
if bb.is_valid:
    print(f"size:       {bb.size} instructions")
    print(f"byte size:  {bb.byte_size}")
    print(f"terminator: {hex(bb.terminator_address)}")
    for insn in bb.instructions():
        print(" ", insn.disassembly)
    for succ in bb.successors():
        print(f"  successor -> {hex(succ)}")

# Function addresses that were identified
for addr in graph.function_addresses():
    print(f"function @ {hex(addr)}")

# Inspect a single function
fn = graph.get_function(0x1000)
if fn.is_valid:
    print(f"blocks:       {fn.num_blocks}")
    print(f"instructions: {fn.num_instructions}")
    print(f"byte size:    {fn.byte_size}")
    print(f"cyclomatic:   {fn.cyclomatic_complexity}")
```

---

## Assembler

```python
cfg = dragon.Configuration(syntax=dragon.Syntax.INTEL)
asm = dragon.Assembler(arch=dragon.Architecture.X86_64, config=cfg)

if not asm.is_valid:
    raise RuntimeError("assembler init failed")

raw = asm.assemble("nop\nret\n")
print(raw.hex())   # 90c3
```

### Round-trip example

```python
source = "push rbp\nmov rbp, rsp\npop rbp\nret\n"

cfg = dragon.Configuration()
asm = dragon.Assembler(dragon.Architecture.X86_64, cfg)
raw = asm.assemble(source)

dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)
graph = dis.disassemble(raw, base_address=0x0)

for insn in graph.instructions():
    print(f"0x{insn.address:x}  {insn.disassembly}")
```

### AT&T syntax

```python
cfg = dragon.Configuration(syntax=dragon.Syntax.ATT)
asm = dragon.Assembler(dragon.Architecture.X86_64, cfg)
raw = asm.assemble("nop\nret\n")
```

---

---

## IGNIL blocks

`Disassembler.analyze()` (not `disassemble()`) populates IGNIL blocks in the
graph. Retrieve one with `graph.get_ignil_block(address)`.

```python
dis   = dragon.Disassembler(dragon.Architecture.X86_64)
# nop; cmp rax, rbx; ret
code  = bytes([0x90, 0x48, 0x39, 0xD8, 0xC3])
graph = dis.analyze(code, base_address=0x1000)

ib = graph.get_ignil_block(0x1000)   # None if no IGNIL block at that address
if ib:
    print(ib.to_string())
    # IGNILBlock @ 0x1000
    #   t0:i64 = SUB(RAX:i64, RBX:i64)  ; @0x1001
    #   ...

    # Individual ops (each yields address, opcode int, index)
    for addr, opc, idx in ib.ops():
        print(f"[{idx}] @{hex(addr)}  {dragon.IGNILOpcode.name(opc)}")
        print(ib.op_to_string(idx))
```

`IGNILBlock` is a borrowed view — it is valid as long as the owning graph
is alive; no `__exit__` / destroy needed.

### Peephole simplification

`simplify()` runs the peephole simplifier to a fixed point: constant
folding/propagation, algebraic identity/annihilator elimination, no-op
width-conversion elimination, and dead-Temp elimination. It is block-local
only — it never crosses into predecessor/successor blocks, and never
removes a `Reg`-defining op even if apparently unused within the block (a
`Reg` may be live-out).

```python
simplified, stats = ib.simplify()   # max_iterations=0 selects the default (8)
print(stats)
# <SimplifyStats constants_folded=2 identities_applied=1 ops_removed=3 iterations=2>
print(simplified.to_string())
```

`simplified` is a new, independently owned `IGNILBlock` — `ib` is left
untouched and both must be cleaned up independently (context manager or GC).

---

## x86 register constants

`dragon.x86` exposes every x86/x86-64 register as a 3-tuple
`(offset, width, name)` that can be splatted directly into
`BackwardSlicer.slice_reg()`:

```python
dragon.x86.RAX   # (0,   64, 'RAX')
dragon.x86.ZF    # (139,  8, 'ZF')
dragon.x86.AH    # (1,    8, 'AH')

# Available groups
# 64-bit: RAX RCX RDX RBX RSP RBP RSI RDI R8..R15 RIP
# 32-bit: EAX ECX .. R15D
# 16-bit: AX CX .. R15W
# 8-bit low: AL CL .. R15B
# 8-bit high: AH CH DH BH
# Flags (8-bit): CF PF AF ZF SF OF DF
```

---

## AArch64 register constants

`dragon.arm` mirrors `dragon.x86` for AArch64 — every register as a 3-tuple
`(offset, width, name)`:

```python
dragon.arm.X0   # (0,   64, 'X0')
dragon.arm.ZF   # (785,  8, 'ZF')
dragon.arm.W0   # (0,   32, 'W0')   -- 32-bit view of X0
dragon.arm.FP   # (232, 64, 'FP')  -- alias for X29
dragon.arm.LR   # (240, 64, 'LR')  -- alias for X30

# Available groups
# 64-bit GPRs: X0..X28 X29(FP) X30(LR) XZR SP PC
# 32-bit views: W0..W28 W29 W30 WZR WSP
# NEON/FP (same 128-bit slot, narrower views): V0..V31/Q0..Q31 (128-bit),
#   D0..D31 (64-bit), S0..S31 (32-bit), H0..H31 (16-bit), B0..B31 (8-bit)
# NZCV flags (8-bit): NF ZF CF VF
```

---

## Backward slicer

`BackwardSlicer` propagates a register value backward through an IGNIL CFG.
Both the slicer and every `SliceExpr` it returns are context managers (owned
handles that must be destroyed).

```python
dis   = dragon.Disassembler(dragon.Architecture.X86_64)
code  = bytes([0x48, 0x39, 0xD8, 0xC3])   # cmp rax, rbx; ret
graph = dis.analyze(code, 0x5000)

with dragon.BackwardSlicer(max_blocks=5) as slicer:
    with slicer.slice_reg(graph, 0x5000, *dragon.x86.ZF) as expr:
        print(expr.to_string())
        # EQ(SUB(RAX:i64, RBX:i64), 0x0:i64)
```

The same call works unchanged for AArch64 — swap the architecture and the
register namespace:

```python
dis   = dragon.Disassembler(dragon.Architecture.ARM64)
code  = bytes([0x1f, 0x14, 0x00, 0x71, 0xc0, 0x03, 0x5f, 0xd6])  # cmp w0, #5 ; ret
graph = dis.analyze(code, 0x5000)

with dragon.BackwardSlicer(max_blocks=5) as slicer:
    with slicer.slice_reg(graph, 0x5000, *dragon.arm.ZF) as expr:
        print(expr.to_string())
        # EQ(SUB(W0:i32, 0x5:i32), 0x0:i32)
```

### Walking the tree

```python
def print_expr(expr, indent=0):
    pad = '  ' * indent
    t = expr.type
    if t == dragon.SliceExprType.CONST:
        print(f"{pad}Const 0x{expr.const_value:x}:i{expr.const_width}")
    elif t == dragon.SliceExprType.SYM_REG:
        print(f"{pad}Reg {expr.symreg_name}:i{expr.symreg_width}")
    elif t == dragon.SliceExprType.BINOP:
        print(f"{pad}{expr.opcode_name}")
        with expr.left()  as l: print_expr(l,  indent + 1)
        with expr.right() as r: print_expr(r, indent + 1)
    elif t == dragon.SliceExprType.UNARYOP:
        print(f"{pad}{expr.opcode_name}")
        with expr.operand() as o: print_expr(o, indent + 1)
    elif t == dragon.SliceExprType.LOAD:
        print(f"{pad}Load:i{expr.load_width}")
        with expr.load_addr() as a: print_expr(a, indent + 1)
    else:
        print(f"{pad}undef")

with dragon.BackwardSlicer() as slicer:
    with slicer.slice_reg(graph, 0x5000, *dragon.x86.ZF) as expr:
        print_expr(expr)
# EQ
#   SUB
#     Reg RAX:i64
#     Reg RBX:i64
#   Const 0x0:i64
```

### SliceExpr properties

| Property / method | Type | Available on |
|---|---|---|
| `type` | `SliceExprType` | all |
| `to_string()` | `str` | all |
| `const_value` | `int` | CONST |
| `const_width` | `int` | CONST |
| `symreg_offset` | `int` | SYM_REG |
| `symreg_width` | `int` | SYM_REG |
| `symreg_name` | `str` | SYM_REG |
| `opcode` | `int` | BINOP / UNARYOP |
| `opcode_name` | `str` | BINOP / UNARYOP |
| `left()` | owned `SliceExpr` | BINOP |
| `right()` | owned `SliceExpr` | BINOP |
| `operand()` | owned `SliceExpr` | UNARYOP |
| `load_addr()` | owned `SliceExpr` | LOAD |
| `load_width` | `int` | LOAD |

### Z3 string representation

Available when dragon was built with `-DDRAGON_USE_Z3=ON`.
Raises `RuntimeError` if Z3 support is not present.

```python
z3_str = expr.to_z3_string()
print(z3_str)   # (bvadd rax #x0000000000000004)
```

The returned string is the Z3 bitvector expression in SMT-LIB2 format —
identical to what you would get from lowering with `SliceExprZ3` in C++.

### Value enumeration and unique-value solving

Also requires `-DDRAGON_USE_Z3=ON`; both raise `RuntimeError` otherwise.
Each call constructs its own Z3 context internally — there is no way to pass
extra Z3 assertions across the C boundary, unlike the C++ API's optional
`constraints` parameter; use the C++ API directly
(`dragon::slice::enumeratedValues` / `solveUnique`) if you need that.

```python
values = expr.enumerate_values()          # max_values=0 -> default cap (256), sorted=True
print(values)                             # e.g. [1, 2, 3, 5, 8]

unique = expr.solve_unique()
print(unique)                             # a single int, or None if not uniquely determined
```

| Method | Returns | Notes |
|---|---|---|
| `enumerate_values(max_values=0, sorted=True)` | `list[int]` | `max_values=0` selects the library default (256) |
| `solve_unique()` | `int` or `None` | `None` when zero or more than one model exists |

---

## SwitchDetector

Recognizes a jump-table indirect-branch pattern from a backward-slice
result (typically the target of an indirect jump, sliced with
`BackwardSlicer` above) and reads the concrete targets out of the table's
raw bytes.

```python
with dragon.BackwardSlicer() as slicer:
    with slicer.slice_reg(graph, block_addr, *dragon.x86.RAX) as jump_target:
        sw = dragon.detect_possible_switch(jump_target, block_addr, graph)
        if sw is not None:
            print(sw)   # <SwitchCandidate table_base=0x... entry_size=8 num_cases=4>

            # `rodata`/`rodata_base` describe a window of the binary's image
            # (e.g. the whole .rodata section) that contains the table.
            targets = dragon.read_table_targets(sw, rodata, rodata_base)
            for i, t in enumerate(targets):
                print(f"case {i} -> 0x{t:x}")
```

`SwitchCandidate` is a plain data object (`table_base`, `entry_size`,
`num_cases`, `index_reg_offset`, `index_reg_width` — the last two use the
same register-address-space convention as `BackwardSlicer.slice_reg()`),
returned by `detect_possible_switch()` and also constructible directly if
you already know the table's shape from other analysis.

| Function | Returns | Notes |
|---|---|---|
| `detect_possible_switch(jump_target, block_addr, graph)` | `SwitchCandidate` or `None` | A returned candidate with `num_cases == 0` means the pattern matched but the bounds guard wasn't found |
| `read_table_targets(candidate, data, base_addr)` | `list[int]` | Empty if `num_cases == 0` or the table falls outside `data` |

Not every indirect jump is a jump table — GCC and Clang often lower a
`switch` to a plain compare chain instead (no jump table at all) when the
case count is small or the cases aren't dense, in which case
`detect_possible_switch` correctly returns `None` and there's nothing
further to recover here.

---

## LLVMLifter

`LLVMLifter` lifts a `Function` to LLVM IR using the IGNIL graph.
`graph` must come from `Disassembler.analyze()` — IGNIL blocks must be present.

```python
dis   = dragon.Disassembler(dragon.Architecture.X86_64)
code  = bytes([
    0x55,                    # push rbp
    0x48, 0x89, 0xE5,        # mov  rbp, rsp
    0x48, 0x01, 0xD8,        # add  rax, rbx
    0x5D,                    # pop  rbp
    0xC3,                    # ret
])
graph = dis.analyze(code, base_address=0x1000)

lifter = dragon.LLVMLifter(arch=dragon.Architecture.X86_64, module_name='my_module')

fn = graph.get_function(0x1000)
if lifter.lift(fn, graph):
    print(lifter.ir())    # raw IR

    lifter.optimize()     # mem2reg → instcombine → GVN → DCE

    print(lifter.ir())    # optimized IR
```

### Optimization passes

Individual passes can be called separately or use `optimize()` for the
recommended pipeline.

```python
lifter.run_mem2reg()      # promotes allocas to SSA values
lifter.run_inst_combine() # folds sub-register mask/OR patterns
lifter.run_gvn()          # eliminates redundant loads
lifter.run_dce()          # removes dead flag computations

# equivalent shorthand:
lifter.optimize()
```

### Method reference

| Method | Returns | Notes |
|---|---|---|
| `lift(function, graph)` | `bool` | `False` if any IGNIL block is missing |
| `ir()` | `str` | Textual LLVM IR for all lifted functions |
| `run_mem2reg()` | — | |
| `run_inst_combine()` | — | |
| `run_gvn()` | — | |
| `run_dce()` | — | |

---

## FLIRT signatures

`SignatureDB` loads FLIRT-style `.pat` signature databases and matches them
against disassembled bytes. Text `.pat` format only (the proprietary binary
`.sig` format is not supported).

`dragon.sigs_crc16(data)` exposes the raw CRC16 primitive used to verify
matches (reflected, poly `0xA001`, init `0xFFFF`, no final XOR — CRC-16/ARC)
standalone — useful for checking it against a real `.pat` file, since it was
only ever verified self-consistently against this codebase's own parser.

```python
db = dragon.SignatureDB()
db.load_directory('./signatures')   # every .pat file in the dir, non-recursive
print(db.signature_count)

# Single candidate: try every signature against code starting at 0x1000.
match = db.match_at(code, 0x1000)
if match:
    address, name, length = match
    print(f'{name} at 0x{address:x}, length {length}')

# Many candidates at once: every entry point against every signature.
# A module can bundle more than one function/offset under one pattern, so
# one entry point may produce more than one match in the result list.
for address, name, length in db.match_entries(image, image_base, [0x1000, 0x1040]):
    print(f'0x{address:x}: {name} (len {length})')
```

### Method reference

| Method | Returns | Notes |
|---|---|---|
| `SignatureDB()` | | |
| `load_directory(path)` | — | Every `.pat` file directly inside `path`, not recursive; unparseable files are silently skipped |
| `load_file(path)` | — | No-op if the file can't be parsed |
| `signature_count` | `int` | |
| `match_at(data, address)` | `(address, name, length)` or `None` | `data` must begin at the candidate function's address |
| `match_entries(image, image_base, entry_points)` | `list[(address, name, length)]` | |
| `dragon.sigs_crc16(data)` | `int` | Module-level function, not a `SignatureDB` method |
| `optimize()` | — | Runs all four passes in the recommended order |

---

## Symbolic execution

Available when the library is built with `-DDRAGON_USE_Z3=ON`.

`SymbolicExecutor` runs lifted IGNIL over Z3 bitvectors: seed the machine, run
a block, then ask the solver what it computed. Registers are the
`(offset, width, name)` tuples from the `x86` / `arm` classes.

```python
import dragon
from dragon import x86

cfg = dragon.Configuration(strategy=dragon.Strategy.LINEAR_SWEEP)
dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)

#   xor rax, rdi ; add rax, rsi ; ret
graph = dis.analyze(bytes([0x48,0x31,0xF8, 0x48,0x01,0xF0, 0xC3]), 0x1000)
block = graph.get_ignil_block(0x1000)

with dragon.SymbolicExecutor() as ex:
    ex.set_register(x86.RAX, 0)
    a = ex.symbolize_register(x86.RDI, 'a')
    b = ex.symbolize_register(x86.RSI, 'b')

    result = ex.run(block)

    rax = ex.read_register(x86.RAX)
    print(ex.proves(rax == a + b))   # True
    print(ex.proves(rax == a * b))   # False
```

State persists across `run()` calls, so walking a path is a sequence of runs.

### Expressions

`SymExpr` operators build expressions rather than computing anything, so `==`
returns a `SymExpr` and not a `bool` — the same convention z3py uses. Plain
ints are widened to the other operand's width automatically.

```python
rax = ex.read_register(x86.RAX)

goal   = rax == 0x1337          # SymExpr, not a bool
masked = (rax & 0xFF) + 1
```

Comparison operators (`<`, `<=`, `>`, `>=`) are **signed**, matching Python's
own integer semantics. Use `ult()`, `ule()`, `ugt()`, `uge()` for the unsigned
versions, `logical_and()` / `logical_or()` / `implies()` to combine booleans,
and `~` for boolean negation.

> Expressions may safely outlive the executor that produced them. A
> `z3::expr` does not own its context, so each handle keeps the executor
> alive itself — which is what makes `SymExpr` objects safe to hold in
> globals, where Python finalizes in an order you do not control.

### Solving — recovering an input

A symbolic region of N bytes creates N variables named
`<region>[0]`..`<region>[N-1]`, which `Solution.bytes()` reads back in address
order.

```python
#   mov rax, [rdi] ; xor rax, rsi ; ret
with dragon.SymbolicExecutor() as ex:
    ex.set_register(x86.RDI, 0x4000)
    ex.set_register(x86.RSI, 0x5A5A5A5A5A5A5A5A)
    ex.symbolize_memory(0x4000, 8, 'flag')
    ex.run(block)

    solution = ex.solve(ex.read_register(x86.RAX) == ex.const(target, 64))
    if solution:
        print(solution.bytes('flag'))    # b'DRAGON!!'
        print(solution.values())         # every bound symbol
```

`solve()` returns `None` when no assignment exists — an unreachable goal is
reported, never guessed at. Narrow the search with `assume()` before running:

```python
for i in range(4):
    byte = ex.read_memory(0x5000 + i, 8)
    ex.assume(byte.uge(ex.const(0x21, 8)))    # printable ASCII only
    ex.assume(byte.ule(ex.const(0x7E, 8)))
```

### Control flow — resolving computed jumps

`RunResult.successors` names the concrete addresses control flow can reach,
each with the guard under which it is reached. A branch whose condition is
provably constant collapses to a single successor; a computed jump has its
target enumerated, which is what recovers a jump table or a VM dispatch.

```python
#   mov esi, [rdi] ; jmp qword ptr [rsi*8 + 0x6000]
with dragon.SymbolicExecutor() as ex:
    ex.map_memory(0x6000, handler_table)
    ex.set_register(x86.RDI, 0x7000)

    opcode = ex.symbolize_memory(0x7000, 4, 'vm_opcode')
    ex.assume(opcode.ult(ex.const(3, 32)))     # the VM's own range check

    result = ex.run(block)
    print(result.complete)                     # True
    print([hex(s.address) for s in result.successors])
    # ['0xaa00', '0xbb00', '0xcc00']
```

`result.complete` is the honesty check: True only when the run succeeded *and*
every address and branch target resolved exactly, so the successor list really
is the complete set. Otherwise `result.warnings` says what had to be left open:

```python
for w in result.warnings:
    print(f"@0x{w.address:x}: {w.message}")
# @0x1002: branch target has more than 256 possible values ...
```

### Modelling notes

- Registers and memory are byte-granular, so sub-register aliasing comes out
  right: `EAX` writes bytes 0–3, `AH` writes byte 1, and a `RAX` read
  concatenates bytes 0–7.
- A 32-bit write clears the upper half of a 64-bit register (x86-64 and
  AArch64). Pass `zero_extend_on_32bit_write=False` for a target where it
  merges instead.
- A symbolic address is concretized by enumerating its satisfying values, up
  to `max_address_solutions`; past that bound the access is reported
  unresolved rather than guessed at.
- Blocks stop at the first terminator. Pass `stop_at_terminator=False` to run
  a trace from `lift_trace()`, whose interior terminators must be stepped
  over.

### API summary

| Symbol | Description |
|---|---|
| `SymbolicExecutor(**options)` | Create an executor (see options below) |
| `set_register(reg, value)` | Seed a register concretely |
| `symbolize_register(reg, name)` → `SymExpr` | Bind a register to a fresh variable |
| `map_memory(addr, data)` | Map a concrete blob |
| `symbolize_memory(addr, size, name)` → `SymExpr` | Fill a region with per-byte variables |
| `assume(cond)` | Constrain the rest of the run |
| `run(block)` → `RunResult` | Execute a block; state persists |
| `read_register(reg)` / `read_memory(addr, width)` → `SymExpr` | What a location now holds |
| `const(value, width)` → `SymExpr` | A concrete bitvector |
| `proves(cond)` → `bool \| None` | Does it hold on every model? `None` = solver gave up |
| `is_satisfiable(cond)` → `bool \| None` | Can it hold at all? |
| `evaluate(expr)` → `int \| None` | Value, only if it is forced to exactly one |
| `enumerate(expr, max)` → `(list, bool)` | Distinct values, and whether that list is complete |
| `solve(goal)` → `Solution \| None` | A satisfying assignment |
| `reset()` | Clear register, memory and constraint state |
| `RunResult.successors` | `Successor(address, taken, condition)` list |
| `RunResult.complete` | True only if that list is exhaustive |
| `RunResult.warnings` | `SymWarning(address, message)` list |
| `RunResult.ops_executed` / `.solver_queries` / `.addresses_concretized` | Run statistics |
| `Solution.value(name)` / `.values()` / `.bytes(region)` | Read the model back |

Constructor options, all deferring to the library default when omitted:
`solver_timeout_ms` (5000), `max_address_solutions` (64),
`max_branch_targets` (256), `zero_extend_on_32bit_write` (True),
`symbolize_unmapped_reads` (True), `stop_at_terminator` (True).

`examples/python/symbolic_execution.py` runs all of the above against real
lifted x86-64.

---

## Enum reference

| Class | Values |
|-------|--------|
| `dragon.Architecture` | `UNKNOWN`, `X86_32`, `X86_64`, `ARM32`, `ARM64`, `RISCV32`, `RISCV64` |
| `dragon.Syntax` | `INTEL`, `ATT` |
| `dragon.Strategy` | `LINEAR_SWEEP`, `RECURSIVE_TRAVERSAL` |
| `dragon.SliceExprType` | `CONST`, `SYM_REG`, `BINOP`, `UNARYOP`, `LOAD`, `UNDEF` |
| `dragon.IGNILOpcode` | `COPY`, `LOAD`, `STORE`, `ADD`, … `UNDEF`, `FADD`, `FSUB`, `FMUL`, `FDIV`, `SITOFP`, `FPTOSI` (42 values) |
| `dragon.SwitchCandidate` | `table_base`, `entry_size`, `num_cases`, `index_reg_offset`, `index_reg_width` (plain data, not an enum) |
| `dragon.SimplifyStats` | `constants_folded`, `identities_applied`, `ops_removed`, `iterations`, `changed` (plain data, not an enum) |
