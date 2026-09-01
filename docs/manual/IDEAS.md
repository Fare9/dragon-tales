# Future manual ideas

Scratch list of chapters/topics worth writing once they make sense — not
committed to yet, just not lost.

- **RISC-V chapter** — once RISC-V has lifter/analysis parity with x86/ARM64
  (currently decode-only, see project memory).
- **x86 vs ARM64 semantics side-by-side** — same C snippet compiled for both,
  comparing the generated IGNIL to highlight architectural differences
  (3-address vs 2-address ops, flag semantics, condition codes).
- **Symbolic slicing with Z3** — walkthrough of `BackwardSlicer` +
  `SliceExprZ3` (see `examples/cpp/z3_path_discovery.cpp`), e.g. "what values
  of `x` reach this branch."
- ~~**Full symbolic execution + Z3 solving**~~ — written, chapter 8. Covers
  solving for an input that satisfies a check, pruning branches the solver
  proves unreachable, and resolving computed jumps (jump tables, VM dispatch)
  to their real targets. Still uncovered and worth a follow-up: driving *many*
  blocks — loop handling and automatic path exploration, which chapter 8
  leaves to the caller.
- **Writing a custom analysis pass** — a tutorial-shaped version of
  `docs/cpp_api.md`'s Graph/BasicBlock/Function APIs, e.g. a simple dead-store
  detector over IGNIL.
- **Porting the manual to a static site** — once the markdown content is
  stable, move it into its own project (MkDocs Material or similar) with
  proper nav/search. This repo's copy stays the source of truth until then.
