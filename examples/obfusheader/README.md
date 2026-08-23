# obfusheader_repro — real control-flow + constant obfuscation, for manual analysis

Four small functions compiled with the **real, unmodified**
[obfusheader.h](https://github.com/ac3ss0r/obfusheader.h) (cloned locally to
`/opt/work/obfusheader.h`) — unlike obfus.h, this one is genuinely
Linux-native (no mingw needed) and doesn't touch x87 `long double` anywhere.

## What's enabled

- **`CFLOW_BRANCHING`**: `#define`s `if`/`for`/`while`/`switch`/`else`
  themselves, wrapping every branch in the *whole translation unit* in
  opaque predicates (`int_proxy()`, a real function that fully inlines).
  A single trivial `if`/`else` compiles to **~500 instructions** — real,
  substantial control-flow obfuscation, not a toy.
- **`CONST_ENCRYPTION`**: compile-time constant encryption via templates
  (`OBF(x)`) — magic numbers never appear as plain immediates.

## The four functions

| Function | Exercises |
|---|---|
| `max_op(a, b)` | comparison + branching |
| `abs_op(a)` | `NEG` + branching |
| `sum_to_n(n)` | a real loop (`INC`-based counter) |
| `is_magic(x)` | `CONST_ENCRYPTION` on a magic-number comparison |

All four verified correct by running the binary (`./obfusheader_repro`).

## Where dragon-tales stands on this one

Checked by decoding the real compiled `.text` end to end (not guessed).
Three categories of "unhandled" instruction showed up:

1. **Not real gaps**: `NOOPL`/`NOOPW`/`NOOP` (padding), `ENDBR64` (landing
   pad marker) — no register output, harmless.
2. **Deliberate anti-disassembly junk**: `OUT32rr`/`IN32rr`/`HLT` —
   privileged instructions that would fault if actually executed, almost
   certainly the "unusual inline ASM blocks... to break decompilers"
   obfusheader.h's own README advertises, sitting in unreachable padding.
3. **Real gaps, not yet fixed**: memory-operand `MOVZX`/`MOVSX`, 8-bit
   memory ALU forms, plain `DIV`/`IMUL rr`, accumulator short-encoding
   forms, `SAR` shift-by-1 — same shape as the fixes already landed this
   session, next in line. Plus real SSE2 `double` arithmetic from
   `int_proxy` (confirmed, not x87) — every function hits this since
   `CFLOW_BRANCHING` is in every branch, so none of the four fully
   resolve in dragon-tales *yet*, even though the program logic itself
   (comparisons, the loop, the arithmetic) is otherwise ordinary code.

None of that blocks opening it in Binary Ninja right now, independent of
where dragon-tales's own coverage lands — see `ToDo/Roadmap.md` for the
live status as fixes land.

## Rebuilding

```sh
g++ -O2 -std=c++17 -I/opt/work/obfusheader.h/include obfusheader_repro.cpp -o obfusheader_repro -w
```
