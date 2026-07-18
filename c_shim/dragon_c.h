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

  typedef enum {
      DRAGON_SYNTAX_INTEL = 0,
      DRAGON_SYNTAX_ATT   = 1,
  } dragon_syntax_t;

  // -- Opaque handles ------------------------------------------------------------

  typedef struct dragon_config      dragon_config_t;
  typedef struct dragon_graph       dragon_graph_t;
  typedef struct dragon_disasm      dragon_disasm_t;
  typedef struct dragon_block       dragon_block_t;
  typedef struct dragon_function    dragon_function_t;

  // dragon_instruction_t is a pointer INTO the graph listing - no destroy needed,
  // valid as long as the owning graph is alive.
  typedef struct dragon_instruction dragon_instruction_t;

  // -- Configuration -------------------------------------------------------------

  dragon_config_t*  dragon_config_create(dragon_syntax_t syntax, dragon_strategy_t strategy);
  void              dragon_config_destroy(dragon_config_t* c);
  dragon_syntax_t   dragon_config_syntax(const dragon_config_t* c);
  dragon_strategy_t dragon_config_strategy(const dragon_config_t* c);

  // LLVM CPU name forwarded to MCSubtargetInfo (e.g. "cortex-a8", "skylake").
  // An empty / NULL value keeps the "generic" baseline for the target triple.
  // ARM32 examples: "arm926ej-s" enables BX/BLX; "cortex-a8" enables Thumb2.
  void        dragon_config_set_cpu(dragon_config_t* c, const char* cpu);
  const char* dragon_config_cpu(const dragon_config_t* c);

  // LLVM feature string forwarded to MCSubtargetInfo (e.g. "+v4t", "+avx2").
  // Comma-separated list; '+' enables a feature, '-' disables it.
  // ARM32 example: "+v4t" adds the BX instruction; "+v7,+neon" targets ARMv7.
  void        dragon_config_set_features(dragon_config_t* c, const char* features);
  const char* dragon_config_features(const dragon_config_t* c);

  // Enable (1) or disable (0) diagnostic logging to stderr.
  void dragon_config_set_logging(dragon_config_t* c, int enabled);
  int  dragon_config_logging(const dragon_config_t* c);

  // -- Graph ---

  dragon_graph_t* dragon_graph_create(dragon_architecture_t arch);
  void            dragon_graph_destroy(dragon_graph_t* g);

  size_t dragon_graph_size(const dragon_graph_t* g);
  int    dragon_graph_empty(const dragon_graph_t* g);

  // Returns pointer into the graph - valid while graph is alive. No destroy.
  const dragon_instruction_t* dragon_graph_get_instruction(
      const dragon_graph_t* g, uint64_t address);

  // Returns heap-allocated views - caller must destroy.
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

  dragon_disasm_t* dragon_disasm_create(dragon_architecture_t arch,
                                        const dragon_config_t* config);
  void             dragon_disasm_destroy(dragon_disasm_t* d);
  int              dragon_disasm_valid(const dragon_disasm_t* d);

  // Disassemble raw bytes and return a graph. Caller destroys the graph.
  dragon_graph_t*  dragon_disasm_disassemble(dragon_disasm_t* d,
                                             const uint8_t* bytes,
                                             size_t len,
                                             uint64_t base_address);

  // Disassemble and run architecture-specific analysis (block and function
  // discovery). Returns a fully populated graph. Caller destroys the graph.
  dragon_graph_t*  dragon_disasm_analyze(dragon_disasm_t* d,
                                         const uint8_t* bytes,
                                         size_t len,
                                         uint64_t base_address);

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

  dragon_asm_t* dragon_asm_create(dragon_architecture_t arch, const dragon_config_t* config);
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

  // -- String helper -------------------------------------------------------------

  // Free a C string returned by any dragon function whose documentation says
  // "free with dragon_cstr_free".  Do NOT use on static strings.
  void dragon_cstr_free(char* s);

  // -- IGNIL opcodes -------------------------------------------------------------

  // Mirror of ignil::Opcode.  Numerical values are guaranteed to match.
  typedef enum {
      DRAGON_IGNIL_COPY  = 0,
      DRAGON_IGNIL_LOAD  = 1,
      DRAGON_IGNIL_STORE = 2,
      DRAGON_IGNIL_ADD   = 3,
      DRAGON_IGNIL_SUB   = 4,
      DRAGON_IGNIL_NEG   = 5,
      DRAGON_IGNIL_MUL   = 6,
      DRAGON_IGNIL_SDIV  = 7,
      DRAGON_IGNIL_UDIV  = 8,
      DRAGON_IGNIL_SMOD  = 9,
      DRAGON_IGNIL_UMOD  = 10,
      DRAGON_IGNIL_AND   = 11,
      DRAGON_IGNIL_OR    = 12,
      DRAGON_IGNIL_XOR   = 13,
      DRAGON_IGNIL_NOT   = 14,
      DRAGON_IGNIL_SHL   = 15,
      DRAGON_IGNIL_SHR   = 16,
      DRAGON_IGNIL_SAR   = 17,
      DRAGON_IGNIL_ZEXT  = 18,
      DRAGON_IGNIL_SEXT  = 19,
      DRAGON_IGNIL_TRUNC = 20,
      DRAGON_IGNIL_EQ    = 21,
      DRAGON_IGNIL_NEQ   = 22,
      DRAGON_IGNIL_ULT   = 23,
      DRAGON_IGNIL_ULE   = 24,
      DRAGON_IGNIL_UGT   = 25,
      DRAGON_IGNIL_UGE   = 26,
      DRAGON_IGNIL_SLT   = 27,
      DRAGON_IGNIL_SLE   = 28,
      DRAGON_IGNIL_SGT   = 29,
      DRAGON_IGNIL_SGE   = 30,
      DRAGON_IGNIL_JMP   = 31,
      DRAGON_IGNIL_CJMP  = 32,
      DRAGON_IGNIL_CALL  = 33,
      DRAGON_IGNIL_RET   = 34,
      DRAGON_IGNIL_UNDEF = 35,
  } dragon_ignil_opcode_t;

  // Returns a static string for the opcode mnemonic (e.g. "ADD", "COPY").
  const char* dragon_ignil_opcode_name(dragon_ignil_opcode_t op);

  // -- IGNIL block ---------------------------------------------------------------

  // Borrowed pointer into the graph - valid while the owning graph is alive.
  // No destroy needed.
  typedef struct dragon_ignil_block dragon_ignil_block_t;

  // Returns NULL if no IGNIL block exists for this address.
  const dragon_ignil_block_t* dragon_graph_get_ignil_block(
      const dragon_graph_t* g, uint64_t addr);

  size_t   dragon_ignil_block_size(const dragon_ignil_block_t* b);
  uint64_t dragon_ignil_block_address(const dragon_ignil_block_t* b);

  // Render the whole block as a multi-line string.  Caller must free with dragon_cstr_free.
  char* dragon_ignil_block_to_string(const dragon_ignil_block_t* b);

  // Render one op (by 0-based index) as a string.  Caller must free with dragon_cstr_free.
  // Returns NULL if index >= block size.
  char* dragon_ignil_block_op_to_string(const dragon_ignil_block_t* b, size_t index);

  // Iterate ops; callback receives machine address, opcode, and 0-based index.
  void dragon_ignil_block_each_op(
      const dragon_ignil_block_t* b,
      void (*cb)(uint64_t op_addr, dragon_ignil_opcode_t opcode, size_t index, void* user),
      void* user);

  // -- SliceExpr -----------------------------------------------------------------

  typedef enum {
      DRAGON_SLICE_CONST   = 0,
      DRAGON_SLICE_SYM_REG = 1,
      DRAGON_SLICE_BINOP   = 2,
      DRAGON_SLICE_UNARYOP = 3,
      DRAGON_SLICE_LOAD    = 4,
      DRAGON_SLICE_UNDEF   = 5,
  } dragon_slice_type_t;

  // Owned handle.  Root and child handles must each be destroyed independently.
  typedef struct dragon_slice_expr dragon_slice_expr_t;

  void                  dragon_slice_expr_destroy(dragon_slice_expr_t* e);
  dragon_slice_type_t   dragon_slice_expr_type(const dragon_slice_expr_t* e);

  // Caller must free the returned string with dragon_cstr_free.
  char* dragon_slice_expr_to_string(const dragon_slice_expr_t* e);

  // Lower a SliceExpr through Z3 and return the Z3 bitvector expression as a
  // string. Only available when the library is built with -DDRAGON_USE_Z3=ON.
  // Caller must free with dragon_cstr_free.
#ifdef DRAGON_WITH_Z3
  char* dragon_slice_expr_to_z3_string(const dragon_slice_expr_t* e);
#endif

  // DRAGON_SLICE_CONST
  uint64_t dragon_slice_expr_const_value(const dragon_slice_expr_t* e);
  uint8_t  dragon_slice_expr_const_width(const dragon_slice_expr_t* e);

  // DRAGON_SLICE_SYM_REG
  uint32_t    dragon_slice_expr_symreg_offset(const dragon_slice_expr_t* e);
  uint8_t     dragon_slice_expr_symreg_width(const dragon_slice_expr_t* e);
  // Returns a pointer into internal storage - valid while the expr is alive.
  // Empty string ("") when no name was set.
  const char* dragon_slice_expr_symreg_name(const dragon_slice_expr_t* e);

  // DRAGON_SLICE_BINOP / DRAGON_SLICE_UNARYOP - shared opcode accessor
  dragon_ignil_opcode_t dragon_slice_expr_opcode(const dragon_slice_expr_t* e);

  // DRAGON_SLICE_BINOP - caller must destroy returned handles.
  dragon_slice_expr_t* dragon_slice_expr_left(const dragon_slice_expr_t* e);
  dragon_slice_expr_t* dragon_slice_expr_right(const dragon_slice_expr_t* e);

  // DRAGON_SLICE_UNARYOP - caller must destroy returned handle.
  dragon_slice_expr_t* dragon_slice_expr_operand(const dragon_slice_expr_t* e);

  // DRAGON_SLICE_LOAD - caller must destroy the address handle.
  dragon_slice_expr_t* dragon_slice_expr_load_addr(const dragon_slice_expr_t* e);
  uint8_t              dragon_slice_expr_load_width(const dragon_slice_expr_t* e);

  // -- BackwardSlicer ------------------------------------------------------------

  typedef struct dragon_slicer dragon_slicer_t;

  // max_blocks caps how many inter-block steps the slicer takes (0 ≤ default = 5).
  dragon_slicer_t* dragon_slicer_create(int max_blocks);
  void             dragon_slicer_destroy(dragon_slicer_t* s);

  // Slice a register backwards through the CFG starting at block_addr.
  // reg_offset and reg_width identify the architectural register in the IGNIL
  // register address space (see x86IGNILRegs.hpp for x86 constants, or
  // armIGNILRegs.hpp for AArch64 constants).
  // reg_name is an optional human-readable label carried into SymReg results
  // when the value cannot be resolved (NULL or "" means no name).
  // Returns an owned SliceExpr; caller must destroy with dragon_slice_expr_destroy.
  dragon_slice_expr_t* dragon_slicer_slice_reg(
      const dragon_slicer_t* s,
      const dragon_graph_t*  g,
      uint64_t    block_addr,
      uint32_t    reg_offset,
      uint8_t     reg_width,
      const char* reg_name);

  // -- LLVMLifter ---------------------------------------------------------------

  typedef struct dragon_llvm_lifter dragon_llvm_lifter_t;

  dragon_llvm_lifter_t* dragon_llvm_lifter_create(dragon_architecture_t arch,
                                                  const char* module_name);
  void dragon_llvm_lifter_destroy(dragon_llvm_lifter_t* l);

  // Lift one function. Returns 1 on success, 0 if any IGNIL block is missing.
  int  dragon_llvm_lifter_lift(dragon_llvm_lifter_t* l,
                               const dragon_function_t* fn,
                               const dragon_graph_t* g);

  // Dump textual LLVM IR. Caller must free with dragon_cstr_free.
  char* dragon_llvm_lifter_ir(const dragon_llvm_lifter_t* l);

  void dragon_llvm_lifter_run_mem2reg(dragon_llvm_lifter_t* l);
  void dragon_llvm_lifter_run_inst_combine(dragon_llvm_lifter_t* l);
  void dragon_llvm_lifter_run_gvn(dragon_llvm_lifter_t* l);
  void dragon_llvm_lifter_run_dce(dragon_llvm_lifter_t* l);

  #ifdef __cplusplus
  }
  #endif