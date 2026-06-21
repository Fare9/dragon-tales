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
| `optimize()` | — | Runs all four passes in the recommended order |

---

## Enum reference

| Class | Values |
|-------|--------|
| `dragon.Architecture` | `UNKNOWN`, `X86_32`, `X86_64`, `ARM32`, `ARM64`, `RISCV32`, `RISCV64` |
| `dragon.Syntax` | `INTEL`, `ATT` |
| `dragon.Strategy` | `LINEAR_SWEEP`, `RECURSIVE_TRAVERSAL` |
| `dragon.SliceExprType` | `CONST`, `SYM_REG`, `BINOP`, `UNARYOP`, `LOAD`, `UNDEF` |
| `dragon.IGNILOpcode` | `COPY`, `LOAD`, `STORE`, `ADD`, … `UNDEF` (35 values) |
