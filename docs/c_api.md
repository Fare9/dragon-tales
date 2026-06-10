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
