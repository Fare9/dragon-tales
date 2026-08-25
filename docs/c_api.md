# C API

Include `dragon_c.h` and link against `libdragon_c`.

```c
#include <dragon_c.h>
#include <stdio.h>
```

---

## Configuration

`dragon_config_t` is an opaque handle that groups syntax, disassembly strategy,
and the LLVM target knobs `cpu` and `features`.
It must be destroyed with `dragon_config_destroy` when no longer needed.

```c
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);

// Inspect current settings
dragon_syntax_t   s = dragon_config_syntax(config);
dragon_strategy_t r = dragon_config_strategy(config);

dragon_config_destroy(config);
```

### Targeting a specific CPU or ISA revision

`dragon_config_set_cpu` and `dragon_config_set_features` forward strings
directly to LLVM's `MCSubtargetInfo`. Both default to `""` (LLVM "generic"
baseline). Set them before passing the config to a disassembler or assembler.

```c
// ARM32 — enable BX/BLX (ARMv4T, not present in the generic baseline)
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_config_set_cpu(config, "arm926ej-s");   // ARMv5TEJ
dragon_disasm_t* dis = dragon_disasm_create(DRAGON_ARCH_ARM32, config);
dragon_config_destroy(config);

// ARM32 — use feature flags instead of a CPU name
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_config_set_features(config, "+v7,+neon");
dragon_disasm_t* dis = dragon_disasm_create(DRAGON_ARCH_ARM32, config);
dragon_config_destroy(config);

// RISC-V 64 — multiply, atomics, compressed extensions
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_config_set_features(config, "+m,+a,+c");
dragon_disasm_t* dis = dragon_disasm_create(DRAGON_ARCH_RISCV64, config);
dragon_config_destroy(config);
```

**API reference**

| Function | Description |
|---|---|
| `dragon_config_set_cpu(c, "cortex-a8")` | Set CPU name |
| `dragon_config_cpu(c)` | Get current CPU name (`""` = generic) |
| `dragon_config_set_features(c, "+v4t")` | Set feature string |
| `dragon_config_features(c)` | Get current feature string |

---

## Disassembler

Create a `dragon_disasm_t` from an architecture and a configuration, then call
`dragon_disasm_disassemble` for each buffer you want to decode.
The returned graph must be destroyed when done.

```c
// x86-64: nop; ret
static const uint8_t code[] = { 0x90, 0xC3 };

dragon_config_t*  config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                                DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_disasm_t*  dis    = dragon_disasm_create(DRAGON_ARCH_X86_64, config);
dragon_config_destroy(config);   // config is copied into the disassembler

if (!dragon_disasm_valid(dis)) { /* handle error */ }

dragon_graph_t* graph = dragon_disasm_disassemble(dis, code, sizeof(code),
                                                  /*base_address=*/0x1000);

dragon_disasm_destroy(dis);

if (!graph) { /* handle error */ }

// -- iterate instructions via callback
void print_insn(const dragon_instruction_t* insn, void* user) {
    printf("0x%llx  %s\n",
           (unsigned long long)dragon_instruction_address(insn),
           dragon_instruction_disassembly(insn));
}

dragon_graph_each_instruction(graph, print_insn, NULL);

dragon_graph_destroy(graph);
```

### Instruction flags

```c
void inspect_insn(const dragon_instruction_t* insn, void* user) {
    if (dragon_instruction_is_call(insn))
        printf("  call at 0x%llx\n", dragon_instruction_address(insn));
    if (dragon_instruction_is_jump(insn))
        printf("  jump at 0x%llx\n", dragon_instruction_address(insn));
    if (dragon_instruction_is_return(insn))
        printf("  ret  at 0x%llx\n", dragon_instruction_address(insn));
}

dragon_graph_each_instruction(graph, inspect_insn, NULL);
```

### Raw bytes of an instruction

```c
void print_bytes(const dragon_instruction_t* insn, void* user) {
    size_t len;
    const uint8_t* b = dragon_instruction_bytes(insn, &len);
    for (size_t i = 0; i < len; i++)
        printf("%02x ", b[i]);
    printf("\n");
}
```

### Recursive traversal

```c
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_RECURSIVE_TRAVERSAL);
dragon_disasm_t* dis = dragon_disasm_create(DRAGON_ARCH_X86_64, config);
dragon_config_destroy(config);
```

---

## Graph: blocks and functions

```c
// Look up a single instruction by address
const dragon_instruction_t* insn = dragon_graph_get_instruction(graph, 0x1000);
if (insn)
    printf("%s\n", dragon_instruction_disassembly(insn));

// Iterate block start addresses
void on_block(uint64_t addr, void* user) {
    printf("block @ 0x%llx\n", (unsigned long long)addr);
}
dragon_graph_each_block_address(graph, on_block, NULL);

// Get a block and walk its instructions
dragon_block_t* bb = dragon_graph_get_block(graph, 0x1000);
if (dragon_block_valid(bb)) {
    printf("block size: %zu instructions\n", dragon_block_size(bb));
    dragon_block_each_instruction(bb, print_insn, NULL);
}
dragon_block_destroy(bb);

// Get a function
dragon_function_t* fn = dragon_graph_get_function(graph, 0x1000);
if (dragon_function_valid(fn)) {
    printf("blocks:       %zu\n", dragon_function_num_blocks(fn));
    printf("instructions: %zu\n", dragon_function_num_instructions(fn));
    printf("cyclomatic:   %zu\n", dragon_function_cyclomatic_complexity(fn));
}
dragon_function_destroy(fn);
```

---

## Assembler

```c
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_asm_t* asm_ = dragon_asm_create(DRAGON_ARCH_X86_64, config);
dragon_config_destroy(config);

if (!dragon_asm_valid(asm_)) { /* handle error */ }

size_t   out_len;
uint8_t* bytes = dragon_asm_assemble(asm_, "nop\nret\n", &out_len);
if (!bytes) { /* assembly failed */ }

for (size_t i = 0; i < out_len; i++)
    printf("%02x ", bytes[i]);
// 90 c3

dragon_bytes_free(bytes);
dragon_asm_destroy(asm_);
```

### AT&T syntax

```c
dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_ATT,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_asm_t* asm_ = dragon_asm_create(DRAGON_ARCH_X86_64, config);
dragon_config_destroy(config);
```

---

## IGNIL blocks

`dragon_disasm_analyze` populates both the CFG and the IGNIL blocks in the
returned graph. Use `dragon_graph_get_ignil_block` to retrieve a block by
address (borrowed pointer — no destroy needed, valid while the graph is alive).

```c
// nop; cmp rax, rbx; ret
static const uint8_t code[] = { 0x90, 0x48, 0x39, 0xD8, 0xC3 };

dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_disasm_t* dis    = dragon_disasm_create(DRAGON_ARCH_X86_64, config);
dragon_config_destroy(config);

dragon_graph_t* graph = dragon_disasm_analyze(dis, code, sizeof(code), 0x1000);
dragon_disasm_destroy(dis);

const dragon_ignil_block_t* ib = dragon_graph_get_ignil_block(graph, 0x1000);
if (ib) {
    // Render the whole block
    char* s = dragon_ignil_block_to_string(ib);
    printf("%s", s);
    dragon_cstr_free(s);

    // Iterate ops
    void on_op(uint64_t addr, dragon_ignil_opcode_t opc, size_t idx, void* u) {
        printf("  [%zu] @0x%llx  %s\n", idx, (unsigned long long)addr,
               dragon_ignil_opcode_name(opc));
    }
    dragon_ignil_block_each_op(ib, on_op, NULL);
}

dragon_graph_destroy(graph);
```

**IGNIL block API summary**

| Function | Returns | Notes |
|---|---|---|
| `dragon_graph_get_ignil_block(g, addr)` | borrowed `const dragon_ignil_block_t*` | NULL if no IGNIL block at addr |
| `dragon_ignil_block_address(b)` | `uint64_t` | Start address |
| `dragon_ignil_block_size(b)` | `size_t` | Op count |
| `dragon_ignil_block_to_string(b)` | `char*` (heap) | Free with `dragon_cstr_free` |
| `dragon_ignil_block_op_to_string(b, idx)` | `char*` (heap) | Single op; NULL if idx ≥ size |
| `dragon_ignil_block_each_op(b, cb, user)` | — | Callback receives addr, opcode, index |
| `dragon_ignil_opcode_name(opc)` | `const char*` (static) | E.g. `"ADD"`, `"COPY"` |

### Trace lifting

`dragon_disasm_lift_trace` lifts an arbitrary linear sequence of
already-decoded instructions straight to IGNIL — e.g. a captured execution
trace (Unicorn/QBDI/Frida/Intel PT) that isn't claimed to form a single
valid basic block or a coherent CFG. It bypasses `dragon_disasm_analyze`'s
CFG/function/block-validity checks entirely: each instruction is lifted
independently and the results concatenated in order, with no synthesized
fall-through terminator.

Unlike `dragon_graph_get_ignil_block`'s borrowed result, the returned block
is **owned by the caller** and must be destroyed with
`dragon_ignil_block_destroy` — never call that function on a block that came
from `dragon_graph_get_ignil_block` (borrowed, owned by the graph; doing so
double-frees it).

```c
dragon_graph_t* graph = dragon_disasm_disassemble(dis, code, sizeof(code), 0x1000);

// Collect borrowed instruction pointers to feed as a trace — here just the
// whole linear decode, but they could come from anywhere (e.g. addresses
// visited by a real emulator run), in any order, even non-contiguous.
const dragon_instruction_t* trace[/* count */];
size_t n = 0;
void collect(const dragon_instruction_t* i, void* user) {
    trace[n++] = i;
}
dragon_graph_each_instruction(graph, collect, NULL);

dragon_ignil_block_t* block = dragon_disasm_lift_trace(dis, trace, n);
char* s = dragon_ignil_block_to_string(block);
printf("%s", s);
dragon_cstr_free(s);

dragon_ignil_block_destroy(block);   // owned — NOT dragon_graph_get_ignil_block's block
dragon_graph_destroy(graph);
```

| Function | Returns | Notes |
|---|---|---|
| `dragon_disasm_lift_trace(d, instructions, count)` | **owned** `dragon_ignil_block_t*` | Empty block for RISC-V or an empty trace |
| `dragon_ignil_block_destroy(b)` | — | Only for blocks from `dragon_disasm_lift_trace` |

### Peephole simplification

`dragon_ignil_simplify_block` runs `ignil::simplifyBlock` to a fixed point:
constant folding/propagation, algebraic identity/annihilator elimination,
no-op width-conversion elimination, and dead-Temp elimination. It is
block-local only — it never crosses into predecessor/successor blocks, and
never removes a `Reg`-defining op even if apparently unused within the block
(a `Reg` may be live-out).

```c
dragon_ignil_block_t* block = dragon_disasm_lift_trace(dis, trace, n);

size_t folded, identities, removed, iterations;
dragon_ignil_block_t* simplified = dragon_ignil_simplify_block(
    block, /*max_iterations=*/0,   // 0 selects the library default (8)
    &folded, &identities, &removed, &iterations);

printf("folded=%zu identities=%zu removed=%zu iterations=%zu\n",
       folded, identities, removed, iterations);

char* s = dragon_ignil_block_to_string(simplified);
printf("%s", s);
dragon_cstr_free(s);

dragon_ignil_block_destroy(simplified);   // owned, independent of `block`
dragon_ignil_block_destroy(block);
```

| Function | Returns | Notes |
|---|---|---|
| `dragon_ignil_simplify_block(b, max_iterations, out_folded, out_identities, out_removed, out_iterations)` | **owned** `dragon_ignil_block_t*` | Every `out_*` pointer may be NULL; `max_iterations = 0` selects the default (8) |

---

## Backward slicer

`dragon_slicer_t` wraps `BackwardSlicer`. `dragon_slice_expr_t` is an owned
handle to a node in the expression tree; child handles (left, right, operand,
load_addr) are also owned and must each be destroyed independently.

```c
// Slice ZF backward starting from block 0x1000
dragon_slicer_t* slicer = dragon_slicer_create(/*max_blocks=*/5);

dragon_slice_expr_t* expr = dragon_slicer_slice_reg(
    slicer, graph,
    /*block_addr=*/0x1000,
    /*reg_offset=*/139,   // ZF offset
    /*reg_width =*/8,     // ZF is 8-bit in IGNIL
    /*reg_name  =*/"ZF");

// Render the whole expression
char* s = dragon_slice_expr_to_string(expr);
printf("%s\n", s);   // EQ(SUB(RAX:i64, RBX:i64), 0x0:i64)
dragon_cstr_free(s);

// Walk the tree
if (dragon_slice_expr_type(expr) == DRAGON_SLICE_BINOP) {
    dragon_ignil_opcode_t opc = dragon_slice_expr_opcode(expr);
    printf("opcode: %s\n", dragon_ignil_opcode_name(opc));

    dragon_slice_expr_t* left  = dragon_slice_expr_left(expr);
    dragon_slice_expr_t* right = dragon_slice_expr_right(expr);

    // left is another BINOP (SUB), right is a CONST
    if (dragon_slice_expr_type(right) == DRAGON_SLICE_CONST)
        printf("rhs: 0x%llx:i%d\n",
               (unsigned long long)dragon_slice_expr_const_value(right),
               (int)dragon_slice_expr_const_width(right));

    dragon_slice_expr_destroy(left);
    dragon_slice_expr_destroy(right);
}

dragon_slice_expr_destroy(expr);
dragon_slicer_destroy(slicer);
```

**SliceExpr API summary**

| Function | Type | Notes |
|---|---|---|
| `dragon_slice_expr_destroy(e)` | — | Must be called for every owned handle |
| `dragon_slice_expr_type(e)` | `dragon_slice_type_t` | CONST / SYM_REG / BINOP / UNARYOP / LOAD / UNDEF |
| `dragon_slice_expr_to_string(e)` | `char*` (heap) | Free with `dragon_cstr_free` |
| `dragon_slice_expr_const_value(e)` | `uint64_t` | CONST nodes only |
| `dragon_slice_expr_const_width(e)` | `uint8_t` | CONST nodes only |
| `dragon_slice_expr_symreg_offset(e)` | `uint32_t` | SYM_REG nodes only |
| `dragon_slice_expr_symreg_width(e)` | `uint8_t` | SYM_REG nodes only |
| `dragon_slice_expr_symreg_name(e)` | `const char*` (borrowed) | SYM_REG; `""` if no name was set |
| `dragon_slice_expr_opcode(e)` | `dragon_ignil_opcode_t` | BINOP / UNARYOP |
| `dragon_slice_expr_left(e)` | owned `dragon_slice_expr_t*` | BINOP |
| `dragon_slice_expr_right(e)` | owned `dragon_slice_expr_t*` | BINOP |
| `dragon_slice_expr_operand(e)` | owned `dragon_slice_expr_t*` | UNARYOP |
| `dragon_slice_expr_load_addr(e)` | owned `dragon_slice_expr_t*` | LOAD |
| `dragon_slice_expr_load_width(e)` | `uint8_t` | LOAD |

### Z3 string representation

Available when the library is built with `-DDRAGON_USE_Z3=ON`
(`DRAGON_WITH_Z3` is defined).

```c
#ifdef DRAGON_WITH_Z3
char* z3s = dragon_slice_expr_to_z3_string(expr);
printf("Z3: %s\n", z3s);   // (bvadd rax #x0000000000000004)
dragon_cstr_free(z3s);
#endif
```

The returned string is a heap-allocated Z3 expression in SMT-LIB2 format.
Free it with `dragon_cstr_free`. Returns `"undef"` for a null pointer.

### Value enumeration and unique-value solving

Also gated on `-DDRAGON_USE_Z3=ON`. Both functions construct and own their
Z3 context internally — there is no way to pass extra Z3 assertions across
the C boundary, unlike the C++ API's optional `constraints` parameter; use
the C++ API directly (`dragon::slice::enumeratedValues` /
`dragon::slice::solveUnique`) if you need that.

```c
#ifdef DRAGON_WITH_Z3
size_t count;
uint64_t* values = dragon_slice_expr_enumerate_values(
    expr, /*max_values=*/0, /*sorted=*/1, &count);   // 0 -> default cap (256)
for (size_t i = 0; i < count; ++i)
    printf("value[%zu] = 0x%llx\n", i, (unsigned long long)values[i]);
dragon_u64_array_free(values);

uint64_t unique;
if (dragon_slice_expr_solve_unique(expr, &unique))
    printf("unique value: 0x%llx\n", (unsigned long long)unique);
#endif
```

| Function | Returns | Notes |
|---|---|---|
| `dragon_slice_expr_enumerate_values(e, max_values, sorted, out_count)` | **owned** `uint64_t*` | Free with `dragon_u64_array_free`; never NULL except on a null `e` |
| `dragon_slice_expr_solve_unique(e, out_value)` | `int` | 1 and writes `*out_value` iff exactly one model exists |
| `dragon_u64_array_free(arr)` | — | Frees arrays from either function above (and from `dragon_read_table_targets`, see SwitchDetector) |

---

## SwitchDetector

Wraps `dragon::slice::detectPossibleSwitch` / `readTableTargets` — recognizes
a jump-table indirect-branch pattern from a backward-slice result (typically
the target of an indirect `JMP`, sliced with the backward slicer above) and
reads the concrete targets out of the table's raw bytes.

```c
dragon_slice_expr_t* jump_target = dragon_slicer_slice_reg(
    slicer, graph, block_addr, /*reg_offset=*/0, /*reg_width=*/64, "");

dragon_switch_candidate_t sw;
if (dragon_detect_possible_switch(jump_target, block_addr, graph, &sw)) {
    printf("table_base=0x%llx entry_size=%u num_cases=%u\n",
           (unsigned long long)sw.table_base, sw.entry_size, sw.num_cases);

    // `table_bytes`/`table_base_addr` describe a window of the binary's
    // image that contains the table (e.g. the whole .rodata section).
    size_t count;
    uint64_t* targets = dragon_read_table_targets(
        &sw, table_bytes, table_bytes_len, table_base_addr, &count);
    for (size_t i = 0; i < count; ++i)
        printf("case %zu -> 0x%llx\n", i, (unsigned long long)targets[i]);
    dragon_u64_array_free(targets);
}

dragon_slice_expr_destroy(jump_target);
```

`dragon_switch_candidate_t` is a plain value struct (not an opaque handle,
nothing to destroy):

```c
typedef struct {
    uint64_t table_base;       // base address of the jump table in the image
    uint32_t entry_size;       // bytes per entry: 4 or 8
    uint32_t num_cases;        // 0 means the bounds check was not found
    uint32_t index_reg_offset; // index register, same convention as
    uint8_t  index_reg_width;  // dragon_slicer_slice_reg's reg_offset/reg_width
} dragon_switch_candidate_t;
```

| Function | Returns | Notes |
|---|---|---|
| `dragon_detect_possible_switch(jump_target, block_addr, g, out)` | `int` | 1 and populates `*out` on a match; a matched `num_cases == 0` means the bounds guard wasn't found |
| `dragon_read_table_targets(sw, bytes, bytes_len, base_addr, out_count)` | **owned** `uint64_t*` | Free with `dragon_u64_array_free`; empty (not NULL) if `num_cases == 0` or the table falls outside `bytes` |

Not every indirect jump is a jump table — GCC and Clang often lower a
`switch` to a plain compare chain instead (no jump table at all) when the
case count is small or the cases aren't dense, in which case
`dragon_detect_possible_switch` correctly returns 0 and there's nothing
further to recover here.

---

## LLVMLifter

`dragon_llvm_lifter_t` wraps the C++ `LLVMLifter`. It must be destroyed with
`dragon_llvm_lifter_destroy` when no longer needed. `ir` strings are
heap-allocated and must be freed with `dragon_cstr_free`.

`graph` must come from `dragon_disasm_analyze` — IGNIL blocks must be present.

```c
static const uint8_t code[] = {
    0x55,                    // push rbp
    0x48, 0x89, 0xE5,        // mov  rbp, rsp
    0x48, 0x01, 0xD8,        // add  rax, rbx
    0x5D,                    // pop  rbp
    0xC3,                    // ret
};

dragon_config_t* config = dragon_config_create(DRAGON_SYNTAX_INTEL,
                                               DRAGON_STRATEGY_LINEAR_SWEEP);
dragon_disasm_t* dis    = dragon_disasm_create(DRAGON_ARCH_X86_64, config);
dragon_config_destroy(config);

dragon_graph_t*    graph  = dragon_disasm_analyze(dis, code, sizeof(code), 0x1000);
dragon_function_t* fn     = dragon_graph_get_function(graph, 0x1000);
dragon_disasm_destroy(dis);

dragon_llvm_lifter_t* lifter = dragon_llvm_lifter_create(DRAGON_ARCH_X86_64,
                                                          "my_module");

if (dragon_llvm_lifter_lift(lifter, fn, graph)) {
    // Raw IR
    char* ir = dragon_llvm_lifter_ir(lifter);
    printf("%s", ir);
    dragon_cstr_free(ir);

    // Optimize
    dragon_llvm_lifter_run_mem2reg(lifter);
    dragon_llvm_lifter_run_inst_combine(lifter);
    dragon_llvm_lifter_run_gvn(lifter);
    dragon_llvm_lifter_run_dce(lifter);

    // Optimized IR
    ir = dragon_llvm_lifter_ir(lifter);
    printf("%s", ir);
    dragon_cstr_free(ir);
}

dragon_function_destroy(fn);
dragon_llvm_lifter_destroy(lifter);
dragon_graph_destroy(graph);
```

**API summary**

| Function | Returns | Notes |
|---|---|---|
| `dragon_llvm_lifter_create(arch, module_name)` | `dragon_llvm_lifter_t*` | `module_name` may be NULL (defaults to `"lifted"`) |
| `dragon_llvm_lifter_destroy(l)` | — | |
| `dragon_llvm_lifter_lift(l, fn, graph)` | `int` (0/1) | 0 if any IGNIL block is missing |
| `dragon_llvm_lifter_ir(l)` | `char*` (heap) | Free with `dragon_cstr_free` |
| `dragon_llvm_lifter_run_mem2reg(l)` | — | Promotes allocas → SSA |
| `dragon_llvm_lifter_run_inst_combine(l)` | — | Folds sub-register patterns |
| `dragon_llvm_lifter_run_gvn(l)` | — | Eliminates redundant loads |
| `dragon_llvm_lifter_run_dce(l)` | — | Removes dead flag computations |

---

## FLIRT signatures

`dragon_sigdb_t` wraps a `SignatureDB` — a loaded collection of FLIRT-style
`.pat` signatures. Destroy it with `dragon_sigdb_destroy` when no longer
needed.

Text `.pat` format only (the proprietary binary `.sig` format is not
supported). The CRC16 variant used (`dragon_sigs_crc16`) is a standard one
(reflected, poly `0xA001`, init `0xFFFF`, no final XOR — CRC-16/ARC) but was
never verified against a real IDA/FLAIR-generated `.pat` file — if you load
a real signature database and nothing ever matches, check this first.

```c
dragon_sigdb_t* db = dragon_sigdb_create();
dragon_sigdb_load_directory(db, "./signatures");   // every .pat file in the dir
printf("loaded %zu signatures\n", dragon_sigdb_signature_count(db));

// Single candidate: try every signature against bytes starting at `address`.
uint64_t match_addr; char* match_name; uint32_t match_len;
if (dragon_sigdb_match_at(db, code, sizeof(code), 0x1000,
                          &match_addr, &match_name, &match_len)) {
    printf("matched: %s (len %u)\n", match_name, match_len);
    dragon_cstr_free(match_name);
}

// Many candidates at once: every entry point against every signature.
// A module can bundle more than one function/offset under one pattern, so
// one entry point may produce more than one callback invocation.
void on_match(uint64_t addr, const char* name, uint32_t len, void* user) {
    printf("0x%llx: %s (len %u)\n", (unsigned long long)addr, name, len);
}
uint64_t entry_points[] = { 0x1000, 0x1040 };
dragon_sigdb_match_entries(db, image, image_len, image_base,
                           entry_points, 2, on_match, NULL);

dragon_sigdb_destroy(db);
```

**API summary**

| Function | Returns | Notes |
|---|---|---|
| `dragon_sigdb_create()` | `dragon_sigdb_t*` | |
| `dragon_sigdb_destroy(db)` | — | |
| `dragon_sigdb_load_directory(db, dir)` | — | Every `.pat` file directly inside `dir`, not recursive; unparseable files are silently skipped |
| `dragon_sigdb_load_file(db, path)` | — | No-op if the file can't be parsed |
| `dragon_sigdb_signature_count(db)` | `size_t` | |
| `dragon_sigs_crc16(data, len)` | `uint16_t` | The raw CRC16 primitive, exposed standalone for verifying it against real `.pat` data |
| `dragon_sigdb_match_at(db, bytes, len, address, &out_address, &out_name, &out_length)` | `int` (0/1) | `out_name` is heap-allocated — free with `dragon_cstr_free`; any out-pointer may be NULL |
| `dragon_sigdb_match_entries(db, image, image_len, image_base, entry_points, n, cb, user)` | — | Callback receives `(address, name, length, user)`; `name` is borrowed, valid only during the callback |
