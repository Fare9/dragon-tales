# obfush_repro — a constant-obfuscation reproduction for manual analysis

`obfush_repro` is a small compiled binary demonstrating the "Virtualization"
(`VIRT`) feature of [obfus.h](https://github.com/DosX-dev/obfus.h) (cloned
locally at `/opt/work/obfus.h`), adapted to compile and run natively on
Linux. It's the same fixture `dragon-tales`'s own automated test
(`tests/obfush/TestObfush.cpp`) uses `BackwardSlicer` + Z3 against — this
copy is meant for you to open directly in a disassembler (Binary Ninja,
IDA, Ghidra, `objdump`, whatever) and work through by hand.

## Why this isn't the literal obfus.h

Two reasons this is a from-scratch reproduction rather than the real
header, both documented in `obfush_repro.c`'s own comment block:

1. The real obfus.h uses `long double` (x87 extended precision) for every
   value, forcing `fld`/`fstp`/x87 codegen. dragon-tales's `X86IGNILLifter`
   has no x87 or SSE support at all yet, so the real compiled code isn't
   liftable there today.
2. The real header unconditionally `#include <windows.h>`, even in the
   community GCC-compatible fork (`obfus.h/experiments/`) — it's a
   Windows/PE-only tool, and there's no Windows toolchain on this machine.

This reproduction preserves the *exact same structural pattern* as the
real thing, just with `int` instead of `long double`, and without the
Windows-only surrounding scaffolding (anti-debug, string hiding, fake
signatures) that obfus.h layers on top — none of that is used by `VIRT`
itself.

## What's actually going on

```c
#define SALT_CMD  0x1F4
#define SALT_NUM1 23
#define SALT_NUM2 31
#define ENCRYPT(op) (((op) - 1) * ~SALT_CMD)

int VmDispatch(int command, int num1, int num2) {
    command = command / ~SALT_CMD + 1;      // decrypt the opcode
    num1 = -num1 + SALT_NUM1;               // unmask operand 1
    num2 = -num2 + SALT_NUM2;               // unmask operand 2
    switch (command) { case OP_ADD: return num1 + num2; ... }
}

#define VM_ADD(a, b) VmDispatch(ENCRYPT(OP_ADD), -(a)+SALT_NUM1, -(b)+SALT_NUM2)

int compute(int a, int b) { return VM_ADD(a, b); }
```

At every `VM_ADD(a, b)` call site, the opcode (`OP_ADD`) is a **compile-time
constant**, "encrypted" via a fixed public formula and baked directly into
the binary as a literal. Each operand is masked at the call site
(`-(x) + SALT`) and unmasked inside the one shared `VmDispatch` function
(`-masked + SALT`, which algebraically cancels back to `x`). `VmDispatch`
then does a plain `switch` on the decrypted opcode to run the real
operation.

Notably, **there's no VM interpreter loop, no bytecode stream, no jump
table of per-opcode handlers** — every `VM_*` call is just an ordinary
function call to one shared dispatcher, with the "obfuscation" being
entirely in the compile-time constant encryption and the operand masking
arithmetic. This is a materially different (and much weaker) protection
primitive than a real VM-based protector (VMProtect/Themida-style), which
`dragon-tales`'s `tests/toy-vm/` work is built to devirtualize instead.

`compute` (compiled at `-O2`) got tail-call-optimized straight into
`jmp VmDispatch` rather than a `call` — worth noticing when you disassemble
it, since it means `compute`'s entire body is just the masking arithmetic
with no visible return path back into it.

## What dragon-tales recovered automatically

Run `obfush_constant_recovery` (built from `obfush_constant_recovery.cpp`
in this folder, `if(DRAGON_USE_Z3)`-gated in `CMakeLists.txt`) to see the
actual analysis run and print its output: the disassembly, the lifted
IGNIL, the `BackwardSlicer`-recovered expressions, and the Z3 proof, all
narrated. This is the same fixture `tests/obfush/TestObfush.cpp` and
`TestObfushZ3.cpp` assert against, just shown rather than only pass/failed.

In short, `BackwardSlicer` on `compute`'s real compiled bytes:

- recovers the encrypted opcode constant exactly (`0xffdd609b` for
  `OP_ADD`), checked against the same `ENCRYPT` formula recomputed
  independently in the test;
- recovers each masked operand's *formula*, not just its value —
  `SALT_NUM1 - a` and `SALT_NUM2 - b`, where `a`/`b` are `compute`'s own
  (unresolved, symbolic) incoming parameters;
- **formally proves via Z3** (`tests/obfush/TestObfushZ3.cpp`) that the
  recovered masking formula really is the exact algebraic inverse of the
  real unmask step, for every possible input — not just checked to look
  structurally right.

See that test file (and `ToDo/Roadmap.md`'s item 14) for the full writeup.
Everything above should be independently verifiable by hand in a
disassembler — that's the point of this folder.

## Does LLVM actually recover the real operation?

`obfush_constant_recovery` above only looks at `compute`'s call site. The
natural next question — does LLVM's *own* optimizer recover the real
`a + b` once you lift `compute` calling all the way into `VmDispatch`, not
just the masked arguments? — needed a real trace: run
`obfush_full_recovery` (`if(DRAGON_USE_UNICORN)`-gated) to see it answered.

It captures a genuine Unicorn execution of `compute(15, 7)` through the
*real* linked binary (the same `obfush_repro`'s `.text`, not a
reproduction), lifts the captured trace straight to IGNIL and LLVM IR
(`Disassembler::liftTrace` + `LLVMLifter::liftBlock` — the same technique
`tests/toy-vm/` uses), and runs the ordinary `mem2reg`/`instcombine`/
`GVN`/`DCE` pipeline — no custom passes. The optimized IR ends up
containing `%10 = add i32 %5, %3`, written straight to the return
register: the real addition, recovered. JIT-verified against three inputs,
including two never present in the original trace (`compute(100, 23) ==
123`, `compute(-4, 9) == 5`) — both correct, so this is genuine recovery,
not a replay of the one traced answer.

Getting here needed five real `X86IGNILLifter` fixes first
(`MOVSX64rr32`, `SAR32ri`/`SAR64ri`, `IMUL64rri32`/`rri8`/`32rri`/`32rri8`,
`LEA64_32r`) — all hit by `VmDispatch`'s `command / ~SALT_CMD`, which is
GCC's completely ordinary signed-division-by-constant lowering, not
anything obfuscation-specific. See `ToDo/Roadmap.md`'s item 15 for the
full story, including two dead ends worth knowing about if you go looking
at `LLVMLifter::lift()` yourself (no tail-call support; an early "poison"
theory for the wrong result that turned out to be a misdiagnosis — the
real cause was the five missing instruction forms).

## Rebuilding

```sh
# The standalone binary (for opening in a disassembler):
gcc -O2 -masm=intel obfush_repro.c -o obfush_repro -w

# The dragon-tales analysis demos (from the dragon-tales build directory):
cmake --build . --target obfush_constant_recovery obfush_full_recovery
./bin/obfush_constant_recovery
./bin/obfush_full_recovery
```
