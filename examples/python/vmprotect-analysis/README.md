# VMProtect analysis with dragon-tales

A worked example: taking one VMProtect-virtualized x64 function apart with
dragon-tales' Python API — lifting handlers to IGNIL and LLVM IR, naming them by
their symbolic effect with Z3, and recompiling the virtual program back to clean
LLVM IR.

These scripts are the tooling behind the write-up *Dragon power against
VMProtect*. They are kept here as a realistic, end-to-end example of the Python
API rather than as a general-purpose deobfuscator: a different VMProtect build
re-randomizes its handlers, so the addresses below are specific to one binary
while the *method* is not.

## What you need

- dragon-tales built with `-DDRAGON_USE_Z3=ON` (see the top-level README).
- Python packages: `cffi`, `capstone`, `z3-solver`.
- The binaries under analysis. They are **not** shipped here — fetch
  `serial_check.exe` and `serial_check.vmp.exe` from the companion repo
  [Fare9/Dragons-vs-VMs](https://github.com/Fare9/Dragons-vs-VMs).

Nothing needs configuring if you run from a built checkout with the binaries
dropped beside the scripts. Otherwise:

| variable | meaning | default |
|---|---|---|
| `DRAGON_TALES_PY` | directory holding `dragon.py` | this checkout's `python/` |
| `DRAGON_C_LIB` | the built `libdragon_c` shim | found under `build/` |
| `VMP_TARGET_DIR` | directory holding the binaries | this directory |
| `LLVM_BINDIR` | directory holding `opt` / `lli` | whatever is on `PATH` |

```bash
# quickest check that the library and Z3 are wired up correctly
python3 dt_symexec_demo.py

# one handler, lifted to optimized LLVM IR
VMP_TARGET_DIR=/path/to/binaries python3 dt_devirt_handler.py 0x1400e5427
```

## The scripts

Roughly in the order the analysis was done.

| script | what it does |
|---|---|
| `dt_symexec_demo.py` | Smallest possible use of the API: prove `rax == rdi + rsi` symbolically. Start here. |
| `dt_solve_check_serial.py` | Recover a passing serial for the *unprotected* function by loop-unrolled symbolic execution + Z3. |
| `dt_devirt_handler.py` | Lift one VM handler to optimized LLVM IR (`mem2reg`/`instcombine`/`gvn`/`dce`) so the junk collapses. |
| `dt_catalog.py` | Batch-lift all 256 handlers and group them by LLVM-IR *shape*. A first-pass catalogue — it mislabels several groups, which is why the next script exists. |
| `dt_handler_views.py` | The main event. Four views per handler — capstone, IGNIL after simplify + DSE, optimized LLVM IR, and a symbolic run whose results are read back as Z3 terms — and names each handler by its **recovered effect**. Writes `Post-Handlers.md` and `post_views.json`. |
| `dt_vm_symexec.py` | Step the *real* protected VM one native instruction at a time with the serial symbolic, forking at input-dependent VM branches. |
| `dt_vm_trace.py` | The concrete VM instruction stream for one input. |
| `dt_vm_cfg.py` | Guided symbolic exploration: drive the VM down every branch the engine reports, key blocks by VIP, and detect back-edges so a loop comes out as a loop. Emits Graphviz. |
| `dt_vm_lift.py` | Emits LLVM IR from a recovered CFG -- one LLVM basic block per VM block, real instructions per opcode class, `mem2reg`/SROA left to clean up the VM stack. The test that the CFG is usable, not just plausible. |
| `dt_recompile.py` | Concolic recompiler: re-interpret a VM opcode trace with the input symbolic and lower the resulting term to LLVM IR. `--demo` builds, verifies and lowers the recovered logic without needing a trace. |
| `pe_report.py` | Dependency-free PE parser (sections, entropy, imports) used by all of the above. |
| `vmp_paths.py` | Resolves everything environment-specific; no analysis logic. |

## Which parts of the API these exercise

- `Disassembler.disassemble()` / `lift_trace()` — lifting an arbitrary
  instruction sequence as one IGNIL block, so a handler with internal jumps
  becomes a single block.
- `IGNILBlock.simplify()` and `dead_store_eliminate(live_out=…)` — byte-precise
  liveness with only the VM's own state registers live, which is what turns
  VMProtect's junk into a list of dead addresses with no pattern matching.
- `LLVMLifter` — IGNIL to LLVM IR, then LLVM's own passes.
- `SymbolicExecutor` — symbolizing the registers whose role is known, pinning
  them with `assume()`, and reading every changed location back as a Z3 term.
- `snapshot()` / `restore()` / `diff()` — seeding the machine once and rewinding
  per handler instead of rebuilding it, and asking what a block changed in one
  call.

## A note on what is and is not recovered

`dt_recompile.py` reconstructs the original computation from a *trace*, so what
it emits is a straight-line residual program specialized to one path — correct
for the input shape it was traced on, with the loop unrolled and branches
resolved at build time. Recovering the bytecode's control-flow graph (and hence
the loop) is a separate step; see the write-up for where that line sits.
