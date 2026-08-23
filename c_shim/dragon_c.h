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

  // Free a uint64_t array returned by any dragon function whose
  // documentation says "free with dragon_u64_array_free" (e.g.
  // dragon_read_table_targets, dragon_slice_expr_enumerate_values).
  void dragon_u64_array_free(uint64_t* arr);

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
      // Appended after UNDEF, not interleaved with ADD/SUB/MUL above where
      // they'd read more naturally - see ignil::Opcode's own doc comment
      // for why new opcodes always go at the end of this mirror.
      DRAGON_IGNIL_FADD   = 36,
      DRAGON_IGNIL_FSUB   = 37,
      DRAGON_IGNIL_FMUL   = 38,
      DRAGON_IGNIL_FDIV   = 39,
      DRAGON_IGNIL_SITOFP = 40,
      DRAGON_IGNIL_FPTOSI = 41,
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

  // -- Disassembler: trace lifting -------------------------------------------------

  // Lifts an arbitrary linear sequence of already-decoded instructions
  // straight to IGNIL, bypassing CFG/function/block validity analysis - see
  // Disassembler::liftTrace. `instructions` is an array of borrowed pointers
  // (e.g. from dragon_graph_each_instruction / dragon_graph_get_instruction),
  // consulted in the order given; they need not be contiguous or come from
  // the same graph, as long as they were decoded for the same architecture
  // as `d`. Returns an empty block for RISC-V (no IGNIL lifter yet) or an
  // empty trace.
  //
  // Returns an OWNED ignil block that must be destroyed with
  // dragon_ignil_block_destroy - NOT the same as blocks returned by
  // dragon_graph_get_ignil_block, which are borrowed from the graph and must
  // never be destroyed.
  dragon_ignil_block_t* dragon_disasm_lift_trace(
      const dragon_disasm_t* d,
      const dragon_instruction_t* const* instructions,
      size_t count);

  // Destroys a block returned by dragon_disasm_lift_trace. Do NOT call this
  // on a block returned by dragon_graph_get_ignil_block.
  void dragon_ignil_block_destroy(dragon_ignil_block_t* b);

  // -- IGNIL peephole simplifier ---------------------------------------------------

  // Peephole-simplifies `b`: forward constant folding/propagation, algebraic
  // identity/annihilator elimination, no-op width-conversion elimination, and
  // dead-Temp elimination, run to a fixed point (bounded by max_iterations -
  // 0 selects the library default of 8). Block-local only: does not cross
  // into predecessor/successor blocks, and never removes a Reg-defining op
  // even if apparently unused within the block (a Reg may be live-out).
  //
  // Every out_* pointer may be NULL if that count isn't wanted. When non-NULL
  // they report: constants folded, algebraic identities applied, dead ops
  // removed, and fixed-point iterations actually run (see
  // ignil::SimplifyStats).
  //
  // Returns an OWNED block - destroy with dragon_ignil_block_destroy, same as
  // a block from dragon_disasm_lift_trace, never dragon_graph_get_ignil_block.
  dragon_ignil_block_t* dragon_ignil_simplify_block(
      const dragon_ignil_block_t* b, int max_iterations,
      size_t* out_constants_folded, size_t* out_identities_applied,
      size_t* out_ops_removed, size_t* out_iterations);

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

  // Enumerate concrete 64-bit values `e` can take (see
  // slice::enumeratedValues). Constructs and owns its own Z3 context
  // internally - there is no way to pass extra Z3 assertions across this
  // boundary, unlike the C++ API's optional `constraints` parameter; use the
  // C++ API directly if you need that.
  //   max_values: 0 selects the library default (256).
  //   sorted:     non-zero sorts results ascending (library default: sorted).
  //   out_count:  number of values written to the returned array (may be 0).
  // Returns an OWNED array - free with dragon_u64_array_free. Never NULL
  // (returns a zero-length allocation when there are no solutions), except
  // on a null `e`.
  uint64_t* dragon_slice_expr_enumerate_values(const dragon_slice_expr_t* e,
                                               size_t max_values, int sorted,
                                               size_t* out_count);

  // Solve for a single concrete value (see slice::solveUnique). Returns 1
  // and writes *out_value if exactly one model exists, 0 otherwise
  // (unsatisfiable, more than one model, or a null `e`/`out_value`).
  int dragon_slice_expr_solve_unique(const dragon_slice_expr_t* e,
                                     uint64_t* out_value);
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

  // -- SwitchDetector --------------------------------------------------------------

  // Everything extracted from a detected jump-table pattern - mirrors
  // slice::SwitchCandidate. index_reg_offset/width identify the index
  // register in the IGNIL register address space (see x86IGNILRegs.hpp /
  // armIGNILRegs.hpp), the same convention dragon_slicer_slice_reg uses.
  typedef struct {
      uint64_t table_base;    // base address of the jump table in the image
      uint32_t entry_size;    // bytes per entry: 4 or 8
      uint32_t num_cases;     // 0 means the bounds check was not found
      uint32_t index_reg_offset;
      uint8_t  index_reg_width;
  } dragon_switch_candidate_t;

  // Inspect a backward-slice result (typically from dragon_slicer_slice_reg
  // on an indirect jump's target) and determine whether it is a jump-table
  // indirect branch - see slice::detectPossibleSwitch for the exact
  // recognition rules. Returns 1 and populates *out on a match (out->
  // num_cases == 0 means the pattern matched but the bounds guard wasn't
  // found), 0 otherwise (including a null jump_target/g/out).
  int dragon_detect_possible_switch(const dragon_slice_expr_t* jump_target,
                                    uint64_t block_addr,
                                    const dragon_graph_t* g,
                                    dragon_switch_candidate_t* out);

  // Read sw->num_cases pointer-sized little-endian entries from the jump
  // table described by `sw` out of `bytes` (a buffer starting at
  // `base_addr` in the binary's address space) - see
  // slice::readTableTargets. *out_count is 0 (and an empty allocation is
  // still returned, never NULL) when num_cases == 0 or the table extends
  // outside `bytes`. Returns an OWNED array - free with
  // dragon_u64_array_free.
  uint64_t* dragon_read_table_targets(const dragon_switch_candidate_t* sw,
                                      const uint8_t* bytes, size_t bytes_len,
                                      uint64_t base_addr, size_t* out_count);

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

  // -- FLIRT signatures (dragon::sigs) --------------------------------------------

  typedef struct dragon_sigdb dragon_sigdb_t;

  dragon_sigdb_t* dragon_sigdb_create(void);
  void            dragon_sigdb_destroy(dragon_sigdb_t* db);

  // Loads every .pat file directly inside dir (not recursive). Files that
  // fail to parse are silently skipped - compare signature_count() before
  // and after to see how much was actually loaded.
  void dragon_sigdb_load_directory(dragon_sigdb_t* db, const char* dir);

  // Loads a single .pat file. No-op if it can't be parsed.
  void dragon_sigdb_load_file(dragon_sigdb_t* db, const char* path);

  size_t dragon_sigdb_signature_count(const dragon_sigdb_t* db);

  // The CRC16 variant dragon-tales' signature format uses: reflected,
  // polynomial 0xA001 (CRC-16/ARC), init 0xFFFF, no final XOR. Caveat: this
  // could not be verified against a real IDA/FLAIR-generated .pat file (see
  // SignatureMatcher's class doc) - self-consistent with this codebase's own
  // parser/matcher, but the first thing to check if a real signature
  // database is loaded and nothing ever matches.
  uint16_t dragon_sigs_crc16(const uint8_t* data, size_t len);

  // Tries every signature in db against bytes, which must begin at the
  // candidate function's address. Returns 1 and fills the out params if any
  // signature matches (the first match's primary name), 0 otherwise. Any out
  // pointer may be NULL to ignore that field. *out_name receives an owned
  // string - caller must free with dragon_cstr_free.
  int dragon_sigdb_match_at(const dragon_sigdb_t* db,
                            const uint8_t* bytes, size_t len,
                            uint64_t address,
                            uint64_t* out_address, char** out_name,
                            uint32_t* out_length);

  // Tries every address in entry_points against every signature in db.
  // image/image_base describe the byte region entry_points fall within.
  // Unlike match_at, every name a matching signature defines is reported (a
  // module can bundle more than one function/offset under one pattern), so
  // one entry point may produce more than one callback invocation. `name` is
  // borrowed - valid only for the duration of the callback.
  void dragon_sigdb_match_entries(
      const dragon_sigdb_t* db,
      const uint8_t* image, size_t image_len, uint64_t image_base,
      const uint64_t* entry_points, size_t num_entry_points,
      void (*cb)(uint64_t address, const char* name, uint32_t length, void* user),
      void* user);

  #ifdef __cplusplus
  }
  #endif