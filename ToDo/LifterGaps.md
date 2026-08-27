# Known lifter/analysis gaps — found while writing docs/manual/

Three real, verified bugs found on 2026-07-25 while writing the `docs/manual/`
tutorial and checking every code snippet against the actual build (not
guessed). Distinct from `TODO.md`/`ArchitecturalRefactor.txt`/`Done.txt`,
which are pre-C-shim design history — this file tracks concrete, currently-
reproducible gaps in the code as it stands today.

---

## 1. x86 IGNIL lifter silently drops memory-destination MOV/ADD and reg-vs-memory CMP — FIXED

**Status:** fixed. `liftInstruction()` now dispatches `MOV64mi32`/`MOV32mi`
(mem,imm) to `liftMovStore`, `ADD64mr`/`ADD32mr`/`ADD64mi{32,8}`/`ADD32mi{,8}`
to a new `liftAddMem` (load/add/store RMW), the `SUB` equivalents to a new
`liftSubMem`, `CMP64rm`/`CMP32rm` (reg,mem) to a new `liftCmpRegMem`, and
`CMP{64,32}mr`/`CMP{64,32}mi{32,8}` (mem,reg / mem,imm - not in the original
report but the same gap class and common in `-O0` `if` conditions) to a new
`liftCmpMem`. All five now produce real `LOAD`/`ADD`/`SUB`/`STORE` ops and
correctly computed `CF`/`ZF`/`SF`/`OF`, instead of zero ops.
Additionally, `liftInstruction()`'s default case now emits `UNDEF` for any
explicit register-def operand of a still-unhandled opcode (via
`MCInstrDesc::getNumDefs()`), so any *future* gap of this shape fails loud
instead of silently vanishing - side-effect-free instructions (prefetch,
fence, nop, ...) are unaffected since they have no register defs. Covered by
7 new cases in `tests/disassemblers/TestX86IGNILLifter.cpp` reproducing the
exact instruction forms from the `sum_to_n` repro below.

**Original report:**

**Where:** `lib/disassemblers/x86/X86IGNILLifter.cpp`, `liftInstruction()`'s
opcode dispatch (~line 175-233).

**What's wrong:** the dispatch table only recognizes register-destination
forms:
- `MOV`: `opMov64rr_ / opMov32rr_` (reg,reg), `opMov64ri_ / opMov32ri_ /
  opMov64ri32_` (reg,imm), `opMov64rm_ / opMov32rm_` (reg,mem — load),
  `opMov64mr_ / opMov32mr_` (mem,reg — store). **Missing:** mem,imm (e.g.
  `mov dword ptr [rbp-4], 0`).
- `ADD`/`SUB`: only `rr`/`ri`/`ri8` forms. **Missing:** any memory-destination
  RMW form (e.g. `add dword ptr [rbp-8], eax`, `add dword ptr [rbp-8], 1`).
- `CMP`: only `rr`/`ri`/`ri8` forms. **Missing:** `rm` (e.g. `cmp eax, dword
  ptr [rbp-20]`).

These instructions fall through to the `default` case (line ~226), which per
the existing comment ("Unknown - leave unlisted instructions as gaps (no
UNDEF emitted for instructions with no IGNIL-visible side effects, e.g.
prefetch, fence)") is meant for truly side-effect-free instructions — but
these three absolutely have IGNIL-visible side effects. **No `UNDEF` is
emitted either** — the instruction just produces zero ops, silently.

**Verified impact:** any GCC/Clang `-O0` x86-64 function with local
variables hits this immediately, since `-O0` codegen keeps locals on the
stack and writes/compares them via these exact forms. Concretely reproduced
with:
```c
int sum_to_n(int n) { int total = 0; for (int i=1;i<=n;i++) total+=i; return total; }
```
compiled at `-O0`: five instructions (`mov [mem],imm` ×2, `add [mem],reg`,
`add [mem],imm`, `cmp reg,[mem]`) all produced zero IGNIL ops. Worse than a
gap: the `CJMP` immediately after the dropped `cmp` ends up branching on
stale `ZF`/`SF`/`OF` values from a *previous, unrelated* flag-setting
instruction, since nothing recomputed them. Full repro in
`docs/manual/07-a-real-world-example.md` (the `-O1` + `volatile` version
used there sidesteps most of this, but still hits the `mov [mem], 0`
initializer — see that chapter for the exact before/after IGNIL/IR).

**Suggested fix:** add the missing opcode variants to the dispatch table
and give each existing `liftMov`/`liftAdd`/`liftSub`/`liftCmp` helper (or a
new sibling) the memory-operand-decoding path already used by
`liftMovLoad`/`liftMovStore`. At minimum, until fixed, emit `UNDEF` for the
destination rather than nothing, so downstream flag/value reads are
honestly marked unknown instead of silently stale.

---

## 2. `LLVMLifter::lift()` segfaults if called twice on the same instance — NOT REPRODUCIBLE AS REPORTED; a different crash found and FIXED

**Status:** the reported symptom does not reproduce on `dev` as of 2026-08-25.
Checked across six variants — x86 single-block, x86 multi-block, ARM64, with
and without an `ir()` call between lifts, and two *different* functions on one
lifter — plus five consecutive lifts of a real 10-block function from
`examples/obfush/obfush_repro.text.bin`. All returned `true`, no crash.

The hypothesised cause in the original report was also wrong, which is
probably why: `lift()` uses `llvm::Function::Create(..., name, *module_)`, and
LLVM auto-renames a duplicate symbol (`sub_1000.1`) rather than faulting.
There is no member `IRBuilder` to hold a stale insertion point either — and
there wasn't one when the report was written, so nothing about the code's
shape changed in a way that would have fixed it. The original observation was
real but its trigger was never identified; it may have been an artifact of
whatever the manual's session was doing at the time.

**A real crash in `lift()` was found while checking this, and is fixed:**
`lift()` accepted an *invalid* `Function` view and segfaulted on it. Reproduced
with the repo's own committed fixture — analyze `obfush_repro.text.bin`, loop
over the discovered function addresses, lift each: `0xfe0` and `0xff0` (PLT
stubs, below the analyzed base) have zero blocks and crashed on the **first**
`lift()` call, not the second.

Root cause: `allocateRegFile()` opens with
`IRBuilder<> builder(&llfn->getEntryBlock(), ...)`, and a function with no
blocks never gets an entry `BasicBlock` created on `llfn`. The existing
`buildBlockMap()` guard cannot catch it — that function only reports a missing
IGNIL block from *inside* its per-block loop, so with zero blocks it returns an
empty but perfectly valid map and `if (!bbMap) return false` passes vacuously.

Blockless functions are ordinary, not exotic: `discoverFunctionStarts()` trusts
every call target unconditionally (see #3), so any call to an address outside
the disassembled range — a PLT stub in a raw `.text` blob — becomes a function
start with nothing under it.

Fixed by rejecting `!fn.isValid()` at the top of `lift()`, before the
`llvm::Function` is created, so a refused lift leaves nothing in the module.
`Function`'s own constructor already documents that contract ("Sets
`valid_ = false` if address is not a known valid function or has no blocks");
`lift()` simply never checked it. `isValid()` rather than `blocks().empty()`
deliberately — the latter would miss the constructor's other bail-out, an
invalid block *inside* the function, where `blocks_` is partially filled but
the view is still invalid. Covered by
`tests/lifters/TestLLVMLifter.cpp`, verified to `SIGSEGV` with the guard
removed and pass with it in place.

**Two related asymmetries found while fixing this, deliberately left alone:**
`Graph::functions()` filters invalid views out via `isValid()`, but the C
shim's `dragon_graph_each_function_address` returns raw
`validFunctionAddresses()` with no such filter — which is why a C++ caller
iterating `functions()` never saw the blockless function while a Python one
did. And `dragon_graph_get_function` hands back a `Function` regardless of
validity, with no way for a C caller to test `isValid()` at all. Both are
C++/shim parity issues rather than lifter bugs, and belong in their own issue.

**Original report:**

**Where:** `include/dragon/lifters/llvm/LLVMLifter.hpp` /
`lib/lifters/llvm/LLVMLifter.cpp` (`lift()`).

**What's wrong:** calling `lift()` a second time on one `LLVMLifter`
instance segfaults reliably — reproduced even calling it twice with the
*same* `Function`/`Graph` (not just with two different ones), so it's not a
dangling-graph-reference issue. Minimal repro:
```python
lifter = dragon.LLVMLifter(dragon.Architecture.X86_64, "m")
lifter.lift(fn, graph)   # ok
lifter.lift(fn, graph)   # segfault
```
Likely cause (not yet root-caused): `lift()` presumably calls
`module_->getOrInsertFunction`/creates a new `llvm::Function` named
`sub_<addr>` each call without checking whether that symbol already exists
in `module_`, or some per-call builder/context state isn't reset between
invocations.

**Current workaround (documented in `docs/manual/06-moving-to-llvm-ir.md`):**
construct one `LLVMLifter` per function lifted; never reuse an instance
across `lift()` calls.

**Suggested fix:** root-cause via a debug build + backtrace (`gdb --args
python3 -c "..."` or a C++ repro linked with `-g`), then either (a) make
`lift()` safe to call multiple times (check for/handle an existing function
of the same name in `module_`), or (b) if multi-function modules are out of
scope by design, assert/return `false` instead of crashing on a second call.

---

## 3. ARM64 function-start detection misses frame-pointer-omitting leaf functions — FIXED

**Status:** fixed, via the "architectural" fallback this doc's own suggested
fix pointed at: `X86Analysis::analyze()` and `ARMAnalysis::analyze()` now
both seed `baseAddr` (the address the caller explicitly asked
`disassemble()` to start at) into `fnStarts` unconditionally, independent
of whether a prologue pattern matches. Call targets were already trusted
unconditionally (`discoverFunctionStarts` inserts every call callee into
`functionStarts` regardless of prologue shape) — the only gap left was the
*entry* seed address itself, which is exactly the ARM64 leaf-function repro
below (a function disassembled starting at its own address, no calls in or
out). Deliberately did **not** broaden the byte-level prologue pattern scan
itself (NeverD's "relaxed tier" approach) - that scan runs over every
instruction position in the disassembled region, so weaker patterns risk
spurious function starts mid-function; the baseAddr/call-target signals
carry no such risk since they're never applied blindly. Covered by new
regression tests in `TestX86Analysis.cpp` and `TestARMAnalysis.cpp`
("... is still recognized as a function").

**Original report:**

## 3. ARM64 function-start detection misses frame-pointer-omitting leaf functions

**Where:** `lib/disassemblers/arm/armAnalysis.cpp` /
`lib/disassemblers/arm/ARMInfo`-equivalent prologue heuristic (matches the
`isFunctionPrologue` design in `TODO.md`'s ARM64 section: `stp x29, x30,
[sp, ...]!` + `mov x29, sp`).

**What's wrong:** a real ARM64 leaf function (one that makes no calls)
compiled at `-O1`+ commonly has **no** `stp x29, x30` frame setup at all —
there's nothing forcing the compiler to preserve `x29`/`x30` if the function
never calls out and doesn't need to unwind through it. Verified with:
```c
int sum_to_n(int n) { volatile int total=0; for(int i=1;i<=n;i++) total+=i; return total; }
```
cross-compiled with `clang --target=aarch64-linux-gnu -O1` (with and without
`-fno-omit-frame-pointer` — same result either way for this function): the
resulting `.text` disassembles and lifts to IGNIL **perfectly cleanly** (zero
`UNDEF`s, all 3 blocks present, verified), but `graph.function_addresses()`
comes back **empty** — the block at address 0 is never recognized as a
function start, so `graph.get_function()` returns an invalid view and
`LLVMLifter::lift()` has nothing to work with.

**Suggested fix:** the prologue heuristic needs a fallback for
frame-pointer-omitting functions. x86 already has a precedent worth
mirroring: `X86Info::x8664bitIsCommonPrologue()`
(`lib/disassemblers/x86/x86Info.cpp:92-122`) treats a bare `ENDBR64` as a
sufficient function-entry marker on its own — "endbr64 alone is already a
valid CET landing pad / function entry" (the code's own comment) — *without*
requiring `push rbp`/`mov rbp,rsp` to follow. That's precisely why chapter
7's x86 example (also frame-pointer-less, also `-O1`) was recognized as a
function while the ARM64 cross-compile of the *same source* wasn't: it
starts with `endbr64`, which has its own carve-out; ARM64 has no equivalent
fallback when `stp x29,x30`/`mov x29,sp` is absent. AArch64 doesn't have a
single-instruction landing-pad marker as universal as `endbr64`, so the
right fallback is probably architectural rather than a single opcode
special-case — e.g. falling back to "treat this analysis entry address as a
function start if nothing else claimed it," which is effectively what the
`ENDBR64` special-case gives x86 for free in the common case. Full repro and
current-behavior writeup in `docs/manual/07-a-real-world-example.md`,
"Cross-compiling for ARM64" section.
