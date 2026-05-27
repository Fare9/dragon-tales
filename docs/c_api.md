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
