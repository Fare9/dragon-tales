#pragma once

  #include <stddef.h>
  #include <stdint.h>

  #ifdef __cplusplus
  extern "C" {
  #endif

  // -- Enums ---

  typedef enum {
      DRAGON_ARCH_UNKNOWN = 0,
      DRAGON_ARCH_X86_32,
      DRAGON_ARCH_X86_64,
      DRAGON_ARCH_ARM32,
      DRAGON_ARCH_ARM64,
      DRAGON_ARCH_RISCV32,
      DRAGON_ARCH_RISCV64,
  } dragon_architecture_t;

  typedef enum {
      DRAGON_STRATEGY_LINEAR_SWEEP = 0,
      DRAGON_STRATEGY_RECURSIVE_TRAVERSAL,
  } dragon_strategy_t;

  // -- Opaque handles ------------------------------------------------------------

  typedef struct dragon_graph       dragon_graph_t;
  typedef struct dragon_disasm      dragon_disasm_t;
  typedef struct dragon_block       dragon_block_t;
  typedef struct dragon_function    dragon_function_t;

  // dragon_instruction_t is a pointer INTO the graph listing — no destroy needed,
  // valid as long as the owning graph is alive.
  typedef struct dragon_instruction dragon_instruction_t;

  // -- Graph ---

  dragon_graph_t* dragon_graph_create(dragon_architecture_t arch);
  void            dragon_graph_destroy(dragon_graph_t* g);

  size_t dragon_graph_size(const dragon_graph_t* g);
  int    dragon_graph_empty(const dragon_graph_t* g);

  // Returns pointer into the graph — valid while graph is alive. No destroy.
  const dragon_instruction_t* dragon_graph_get_instruction(
      const dragon_graph_t* g, uint64_t address);

  // Returns heap-allocated views — caller must destroy.
  dragon_block_t*    dragon_graph_get_block(const dragon_graph_t* g, uint64_t address);
  dragon_function_t* dragon_graph_get_function(const dragon_graph_t* g, uint64_t address);

  // Iterate valid addresses via callbacks (avoids exposing sets to C).
  void dragon_graph_each_instruction(const dragon_graph_t* g,
      void (*cb)(const dragon_instruction_t*, void* user), void* user);
  void dragon_graph_each_block_address(const dragon_graph_t* g,
      void (*cb)(uint64_t addr, void* user), void* user);
  void dragon_graph_each_function_address(const dragon_graph_t* g,
      void (*cb)(uint64_t addr, void* user), void* user);

  // -- Disassembler --------------------------------------------------------------

  // Simple API: disassemble bytes → returns a graph with instruction listing only.
  // No block/function structure. Caller destroys the graph.
  dragon_graph_t* dragon_disasm_bytes(
      dragon_architecture_t arch,
      dragon_strategy_t     strategy,
      const uint8_t*        bytes,
      size_t                len,
      uint64_t              base_address);

  // Graph-populating API: disassembler holds the image internally.
  dragon_disasm_t* dragon_disasm_create(
      dragon_architecture_t arch,
      const uint8_t*        image,
      size_t                image_len,
      uint64_t              image_base);
  void dragon_disasm_destroy(dragon_disasm_t* d);
  int  dragon_disasm_valid(const dragon_disasm_t* d);

  void dragon_disasm_function(dragon_disasm_t* d, uint64_t address, dragon_graph_t* g);
  void dragon_disasm_all(dragon_disasm_t* d,
      const uint64_t* addrs, size_t count, dragon_graph_t* g);

  // -- Instruction ---------------------------------------------------------------

  uint64_t    dragon_instruction_address(const dragon_instruction_t* i);
  uint8_t     dragon_instruction_size(const dragon_instruction_t* i);
  const char* dragon_instruction_mnemonic(const dragon_instruction_t* i);
  const char* dragon_instruction_operands(const dragon_instruction_t* i);
  const char* dragon_instruction_disassembly(const dragon_instruction_t* i);
  const char* dragon_instruction_pattern(const dragon_instruction_t* i);

  const uint8_t* dragon_instruction_bytes(
      const dragon_instruction_t* i, size_t* out_len);
  const uint8_t* dragon_instruction_chromosome_mask(
      const dragon_instruction_t* i, size_t* out_len);

  int    dragon_instruction_is_block_start(const dragon_instruction_t* i);
  int    dragon_instruction_is_function_start(const dragon_instruction_t* i);
  int    dragon_instruction_is_prologue(const dragon_instruction_t* i);
  int    dragon_instruction_is_return(const dragon_instruction_t* i);
  int    dragon_instruction_is_call(const dragon_instruction_t* i);
  int    dragon_instruction_is_jump(const dragon_instruction_t* i);
  int    dragon_instruction_is_conditional(const dragon_instruction_t* i);
  int    dragon_instruction_is_trap(const dragon_instruction_t* i);
  int    dragon_instruction_has_indirect_target(const dragon_instruction_t* i);
  size_t dragon_instruction_edges(const dragon_instruction_t* i);

  void dragon_instruction_each_branch(const dragon_instruction_t* i,
      void (*cb)(uint64_t addr, void* user), void* user);
  void dragon_instruction_each_callee(const dragon_instruction_t* i,
      void (*cb)(uint64_t addr, void* user), void* user);

  // -- Assembler ----------------------------------------------------------------

  typedef struct dragon_asm dragon_asm_t;

  dragon_asm_t* dragon_asm_create(dragon_architecture_t arch);
  void          dragon_asm_destroy(dragon_asm_t* a);
  int           dragon_asm_valid(const dragon_asm_t* a);

  // Assemble source text to raw bytes. Returns a heap-allocated buffer that
  // the caller must free with dragon_bytes_free(). Returns NULL on failure.
  uint8_t* dragon_asm_assemble(dragon_asm_t* a, const char* source, size_t* out_len);

  // Free a buffer returned by dragon_asm_assemble.
  void dragon_bytes_free(uint8_t* bytes);

  // -- BasicBlock ----------------------------------------------------------------

  void     dragon_block_destroy(dragon_block_t* b);
  int      dragon_block_valid(const dragon_block_t* b);
  uint64_t dragon_block_start_address(const dragon_block_t* b);
  uint64_t dragon_block_terminator_address(const dragon_block_t* b);
  size_t   dragon_block_size(const dragon_block_t* b);      // instruction count
  size_t   dragon_block_byte_size(const dragon_block_t* b);

  void dragon_block_each_instruction(const dragon_block_t* b,
      void (*cb)(const dragon_instruction_t*, void* user), void* user);
  void dragon_block_each_successor(const dragon_block_t* b,
      void (*cb)(uint64_t addr, void* user), void* user);

  // -- Function 

  void     dragon_function_destroy(dragon_function_t* f);
  int      dragon_function_valid(const dragon_function_t* f);
  uint64_t dragon_function_start_address(const dragon_function_t* f);
  size_t   dragon_function_num_blocks(const dragon_function_t* f);
  size_t   dragon_function_num_instructions(const dragon_function_t* f);
  size_t   dragon_function_edges(const dragon_function_t* f);
  size_t   dragon_function_byte_size(const dragon_function_t* f);
  size_t   dragon_function_cyclomatic_complexity(const dragon_function_t* f);

  void dragon_function_each_block(const dragon_function_t* f,
      void (*cb)(const dragon_block_t*, void* user), void* user);

  #ifdef __cplusplus
  }
  #endif