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

## Enum reference

| Class | Values |
|-------|--------|
| `dragon.Architecture` | `UNKNOWN`, `X86_32`, `X86_64`, `ARM32`, `ARM64`, `RISCV32`, `RISCV64` |
| `dragon.Syntax` | `INTEL`, `ATT` |
| `dragon.Strategy` | `LINEAR_SWEEP`, `RECURSIVE_TRAVERSAL` |
