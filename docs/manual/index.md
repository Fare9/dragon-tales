# The dragon-tales Manual

A hands-on walkthrough of dragon-tales, from raw bytes to optimized LLVM IR.
Unlike [`docs/cpp_api.md`](../cpp_api.md), [`docs/c_api.md`](../c_api.md) and
[`docs/python_api.md`](../python_api.md) — which are reference material,
organized by class/function — this manual is organized by *task*: each
chapter builds on the previous one, using one running example (a small
ARM64/x86-64 function) all the way through the pipeline.

Every code snippet in this manual is runnable as-is; most are lightly
trimmed versions of the example programs under [`examples/`](../../examples).

## Chapters

1. [Getting Started](01-getting-started.md) — building dragon-tales and running your first snippet
2. [Assembling Your Code](02-assembling-your-code.md) — turning assembly text into raw machine bytes
3. [Disassembling Your First Binary](03-disassembling-your-first-binary.md) — turning bytes back into instructions
4. [Functions, Basic Blocks and Instructions](04-functions-blocks-instructions.md) — recovering structure from a flat instruction stream
5. [Lifting to IGNIL](05-lifting-to-ignil.md) — dragon-tales' architecture-independent IL
6. [Moving to LLVM IR](06-moving-to-llvm-ir.md) — from IGNIL to optimizable LLVM IR
7. [A Real-World Example](07-a-real-world-example.md) — compiling C, pulling `.text` with `objdump`, and running it through the whole pipeline

## Which API layer?

Examples are shown in **C++** and **Python** side by side where the two
diverge meaningfully. The C shim (`c_shim/dragon_c.h`) is the same shape as
the Python API (it's what the Python bindings wrap) — see
[`docs/c_api.md`](../c_api.md) if you're integrating from C directly.

## Ideas for future chapters

Not yet written, tracked in [IDEAS.md](IDEAS.md).
