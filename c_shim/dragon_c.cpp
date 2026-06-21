#include "dragon_c.h"

#include <dragon/Architecture.hpp>
#ifdef DRAGON_WITH_Z3
#include <dragon/analysis/SliceExprZ3.hpp>
#endif
#include <dragon/assembler/Assembler.hpp>
#include <dragon/lifters/llvm/LLVMLifter.hpp>
#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/disassembler/BasicBlock.hpp>
#include <dragon/Configuration.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Function.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/disassembler/Instruction.hpp>
#include <dragon/analysis/SliceExpr.hpp>

#include <cassert>
#include <memory>
#include <string>
#include <vector>

// -- helpers to convert C++ types back to C enum values
static dragon_syntax_t from_syntax(dragon::Syntax s) {
  return s == dragon::Syntax::ATT ? DRAGON_SYNTAX_ATT : DRAGON_SYNTAX_INTEL;
}
static dragon_strategy_t from_strategy(dragon::DisassemblyStrategy s) {
  return s == dragon::DisassemblyStrategy::RECURSIVE_TRAVERSAL
                 ? DRAGON_STRATEGY_RECURSIVE_TRAVERSAL
                 : DRAGON_STRATEGY_LINEAR_SWEEP;
}

// -- enum mappings
static dragon::Architecture to_arch(dragon_architecture_t a) {
  switch (a) {
    case DRAGON_ARCH_X86_32:
      return dragon::Architecture::X86_32;
    case DRAGON_ARCH_X86_64:
      return dragon::Architecture::X86_64;
    case DRAGON_ARCH_ARM32:
      return dragon::Architecture::ARM32;
    case DRAGON_ARCH_ARM64:
      return dragon::Architecture::ARM64;
    case DRAGON_ARCH_RISCV32:
      return dragon::Architecture::RISCV32;
    case DRAGON_ARCH_RISCV64:
      return dragon::Architecture::RISCV64;
    default:
      return dragon::Architecture::Unknown;
  }
}

static dragon::Syntax to_syntax(dragon_syntax_t s) {
  return s == DRAGON_SYNTAX_ATT ? dragon::Syntax::ATT : dragon::Syntax::INTEL;
}

// -- dragon_config — full definition
struct dragon_config {
  dragon::Configuration config;
};

dragon_config_t *dragon_config_create(dragon_syntax_t syntax,
                                      dragon_strategy_t strategy) {
  dragon::Configuration cfg;
  cfg.syntax = to_syntax(syntax);
  cfg.disassemblyStrategy =
          (strategy == DRAGON_STRATEGY_RECURSIVE_TRAVERSAL)
                  ? dragon::DisassemblyStrategy::RECURSIVE_TRAVERSAL
                  : dragon::DisassemblyStrategy::LINEAR_SWEEP;
  return new dragon_config{cfg};
}

void dragon_config_destroy(dragon_config_t *c) { delete c; }

dragon_syntax_t dragon_config_syntax(const dragon_config_t *c) {
  return from_syntax(c->config.syntax);
}

dragon_strategy_t dragon_config_strategy(const dragon_config_t *c) {
  return from_strategy(c->config.disassemblyStrategy);
}

void dragon_config_set_cpu(dragon_config_t *c, const char *cpu) {
  if (c)
    c->config.cpu = cpu ? cpu : "";
}

const char *dragon_config_cpu(const dragon_config_t *c) {
  return c ? c->config.cpu.c_str() : "";
}

void dragon_config_set_features(dragon_config_t *c, const char *features) {
  if (c)
    c->config.features = features ? features : "";
}

const char *dragon_config_features(const dragon_config_t *c) {
  return c ? c->config.features.c_str() : "";
}

void dragon_config_set_logging(dragon_config_t *c, int enabled) {
  if (c)
    c->config.logging = enabled != 0;
}

int dragon_config_logging(const dragon_config_t *c) {
  return c && c->config.logging ? 1 : 0;
}

// -- dragon_asm_t — full definition
struct dragon_asm {
  dragon::Assembler assembler;
};

dragon_asm_t *dragon_asm_create(dragon_architecture_t arch,
                                const dragon_config_t *config) {
  dragon::Configuration cfg = config ? config->config : dragon::Configuration{};
  return new dragon_asm{dragon::Assembler(to_arch(arch), cfg)};
}

void dragon_asm_destroy(dragon_asm_t *a) { delete a; }

int dragon_asm_valid(const dragon_asm_t *a) {
  return a && a->assembler.isValid() ? 1 : 0;
}

uint8_t *dragon_asm_assemble(dragon_asm_t *a, const char *source,
                             size_t *out_len) {
  if (!a || !source || !out_len)
    return nullptr;
  auto bytes = a->assembler.assemble(source);
  if (bytes.empty()) {
    *out_len = 0;
    return nullptr;
  }
  auto *buf = new uint8_t[bytes.size()];
  std::copy(bytes.begin(), bytes.end(), buf);
  *out_len = bytes.size();
  return buf;
}

void dragon_bytes_free(uint8_t *bytes) { delete[] bytes; }

// -- dragon_disasm_t — full definition
struct dragon_disasm {
  dragon::Architecture arch;
  dragon::Configuration config;
  dragon::Disassembler disassembler;
};

// -- cast helpers
// --------------------------------------------------------------

static dragon::Graph *as_graph(dragon_graph_t *g) {
  return reinterpret_cast<dragon::Graph *>(g);
}
static const dragon::Graph *as_graph(const dragon_graph_t *g) {
  return reinterpret_cast<const dragon::Graph *>(g);
}
static const dragon::Instruction *as_insn(const dragon_instruction_t *i) {
  return reinterpret_cast<const dragon::Instruction *>(i);
}
static const dragon::BasicBlock *as_block(const dragon_block_t *b) {
  return reinterpret_cast<const dragon::BasicBlock *>(b);
}
static const dragon::Function *as_fn(const dragon_function_t *f) {
  return reinterpret_cast<const dragon::Function *>(f);
}

// -- Graph
// ---------------------------------------------------------------------

dragon_graph_t *dragon_graph_create(dragon_architecture_t arch) {
  dragon::Configuration config;
  auto *g = new dragon::Graph(to_arch(arch), config);
  return reinterpret_cast<dragon_graph_t *>(g);
}

void dragon_graph_destroy(dragon_graph_t *g) { delete as_graph(g); }

size_t dragon_graph_size(const dragon_graph_t *g) {
  return as_graph(g)->size();
}
int dragon_graph_empty(const dragon_graph_t *g) {
  return as_graph(g)->empty() ? 1 : 0;
}

const dragon_instruction_t *
dragon_graph_get_instruction(const dragon_graph_t *g, uint64_t address) {
  const dragon::Instruction *i = as_graph(g)->get(address);
  return reinterpret_cast<const dragon_instruction_t *>(i);
}

dragon_block_t *dragon_graph_get_block(const dragon_graph_t *g,
                                       uint64_t address) {
  auto *b = new dragon::BasicBlock(as_graph(g), address);
  return reinterpret_cast<dragon_block_t *>(b);
}

dragon_function_t *dragon_graph_get_function(const dragon_graph_t *g,
                                             uint64_t address) {
  auto *f = new dragon::Function(as_graph(g), address);
  return reinterpret_cast<dragon_function_t *>(f);
}

void dragon_graph_each_instruction(const dragon_graph_t *g,
                                   void (*cb)(const dragon_instruction_t *,
                                              void *),
                                   void *user) {
  for (const auto &[addr, insn]: as_graph(g)->listing())
    cb(reinterpret_cast<const dragon_instruction_t *>(&insn), user);
}

void dragon_graph_each_block_address(const dragon_graph_t *g,
                                     void (*cb)(uint64_t, void *), void *user) {
  for (uint64_t addr: as_graph(g)->blocks_q.valid())
    cb(addr, user);
}

void dragon_graph_each_function_address(const dragon_graph_t *g,
                                        void (*cb)(uint64_t, void *),
                                        void *user) {
  for (uint64_t addr: as_graph(g)->functions_q.valid())
    cb(addr, user);
}

// -- Disassembler
// --------------------------------------------------------------

dragon_disasm_t *dragon_disasm_create(dragon_architecture_t arch,
                                      const dragon_config_t *config) {
  dragon::Configuration cfg = config ? config->config : dragon::Configuration{};
  dragon::Architecture a = to_arch(arch);
  return new dragon_disasm{a, cfg, dragon::Disassembler(a, cfg)};
}

void dragon_disasm_destroy(dragon_disasm_t *d) { delete d; }

int dragon_disasm_valid(const dragon_disasm_t *d) {
  return d && d->disassembler.isValid() ? 1 : 0;
}

dragon_graph_t *dragon_disasm_disassemble(dragon_disasm_t *d,
                                          const uint8_t *bytes, size_t len,
                                          uint64_t base_address) {
  if (!d || !bytes)
    return nullptr;
  auto instrs = d->disassembler.disassemble({bytes, len}, base_address);
  auto *g = new dragon::Graph(d->arch, d->config);
  for (auto &insn: instrs)
    g->addInstruction(std::move(insn));
  return reinterpret_cast<dragon_graph_t *>(g);
}

dragon_graph_t *dragon_disasm_analyze(dragon_disasm_t *d, const uint8_t *bytes,
                                      size_t len, uint64_t base_address) {
  if (!d || !bytes)
    return nullptr;
  auto *g = new dragon::Graph(d->arch, d->config);
  d->disassembler.disassemble({bytes, len}, base_address, *g);
  return reinterpret_cast<dragon_graph_t *>(g);
}

// -- Instruction
// ---------------------------------------------------------------

uint64_t dragon_instruction_address(const dragon_instruction_t *i) {
  return as_insn(i)->address();
}
uint8_t dragon_instruction_size(const dragon_instruction_t *i) {
  return as_insn(i)->size();
}
const char *dragon_instruction_mnemonic(const dragon_instruction_t *i) {
  return as_insn(i)->mnemonic().data();
}
const char *dragon_instruction_operands(const dragon_instruction_t *i) {
  return as_insn(i)->operands().data();
}
const char *dragon_instruction_disassembly(const dragon_instruction_t *i) {
  return as_insn(i)->disassembly().data();
}
const char *dragon_instruction_pattern(const dragon_instruction_t *i) {
  return as_insn(i)->pattern().data();
}

const uint8_t *dragon_instruction_bytes(const dragon_instruction_t *i,
                                        size_t *out_len) {
  const auto &b = as_insn(i)->bytes();
  if (out_len)
    *out_len = b.size();
  return b.data();
}

const uint8_t *dragon_instruction_chromosome_mask(const dragon_instruction_t *i,
                                                  size_t *out_len) {
  const auto &m = as_insn(i)->chromosomeMask();
  if (out_len)
    *out_len = m.size();
  return m.data();
}

int dragon_instruction_is_block_start(const dragon_instruction_t *i) {
  return as_insn(i)->isBlockStart() ? 1 : 0;
}
int dragon_instruction_is_function_start(const dragon_instruction_t *i) {
  return as_insn(i)->isFunctionStart() ? 1 : 0;
}
int dragon_instruction_is_prologue(const dragon_instruction_t *i) {
  return as_insn(i)->isPrologue() ? 1 : 0;
}
int dragon_instruction_is_return(const dragon_instruction_t *i) {
  return as_insn(i)->isReturn() ? 1 : 0;
}
int dragon_instruction_is_call(const dragon_instruction_t *i) {
  return as_insn(i)->isCall() ? 1 : 0;
}
int dragon_instruction_is_jump(const dragon_instruction_t *i) {
  return as_insn(i)->isJump() ? 1 : 0;
}
int dragon_instruction_is_conditional(const dragon_instruction_t *i) {
  return as_insn(i)->isConditional() ? 1 : 0;
}
int dragon_instruction_is_trap(const dragon_instruction_t *i) {
  return as_insn(i)->isTrap() ? 1 : 0;
}
int dragon_instruction_has_indirect_target(const dragon_instruction_t *i) {
  return as_insn(i)->hasIndirectTarget() ? 1 : 0;
}
size_t dragon_instruction_edges(const dragon_instruction_t *i) {
  return as_insn(i)->edges();
}

void dragon_instruction_each_branch(const dragon_instruction_t *i,
                                    void (*cb)(uint64_t, void *), void *user) {
  for (uint64_t addr: as_insn(i)->branches())
    cb(addr, user);
}

void dragon_instruction_each_callee(const dragon_instruction_t *i,
                                    void (*cb)(uint64_t, void *), void *user) {
  for (uint64_t addr: as_insn(i)->callees())
    cb(addr, user);
}

// -- BasicBlock
void dragon_block_destroy(dragon_block_t *b) {
  delete reinterpret_cast<dragon::BasicBlock *>(b);
}
int dragon_block_valid(const dragon_block_t *b) {
  return as_block(b)->isValid() ? 1 : 0;
}
uint64_t dragon_block_start_address(const dragon_block_t *b) {
  return as_block(b)->startAddress();
}
uint64_t dragon_block_terminator_address(const dragon_block_t *b) {
  return as_block(b)->terminatorAddress();
}
size_t dragon_block_size(const dragon_block_t *b) {
  return as_block(b)->size();
}
size_t dragon_block_byte_size(const dragon_block_t *b) {
  return as_block(b)->byteSize();
}

void dragon_block_each_instruction(const dragon_block_t *b,
                                   void (*cb)(const dragon_instruction_t *,
                                              void *),
                                   void *user) {
  for (const dragon::Instruction *i: as_block(b)->instructions())
    cb(reinterpret_cast<const dragon_instruction_t *>(i), user);
}

void dragon_block_each_successor(const dragon_block_t *b,
                                 void (*cb)(uint64_t, void *), void *user) {
  for (const uint64_t addr: as_block(b)->successors())
    cb(addr, user);
}

// -- Function
// ------------------------------------------------------------------

void dragon_function_destroy(dragon_function_t *f) {
  delete reinterpret_cast<dragon::Function *>(f);
}
int dragon_function_valid(const dragon_function_t *f) {
  return as_fn(f)->isValid() ? 1 : 0;
}
uint64_t dragon_function_start_address(const dragon_function_t *f) {
  return as_fn(f)->startAddress();
}
size_t dragon_function_num_blocks(const dragon_function_t *f) {
  return as_fn(f)->numBlocks();
}
size_t dragon_function_num_instructions(const dragon_function_t *f) {
  return as_fn(f)->numInstructions();
}
size_t dragon_function_edges(const dragon_function_t *f) {
  return as_fn(f)->edges();
}
size_t dragon_function_byte_size(const dragon_function_t *f) {
  return as_fn(f)->byteSize();
}
size_t dragon_function_cyclomatic_complexity(const dragon_function_t *f) {
  return as_fn(f)->cyclomaticComplexity();
}

void dragon_function_each_block(const dragon_function_t *f,
                                void (*cb)(const dragon_block_t *, void *),
                                void *user) {
  for (const auto &[addr, block]: as_fn(f)->blocks())
    cb(reinterpret_cast<const dragon_block_t *>(&block), user);
}

// =============================================================================
// String helper
// =============================================================================

void dragon_cstr_free(char *s) { delete[] s; }

static char *dup_string(const std::string &s) {
  char *buf = new char[s.size() + 1];
  s.copy(buf, s.size());
  buf[s.size()] = '\0';
  return buf;
}

// =============================================================================
// IGNIL opcodes
// =============================================================================

// Verify the enum values match ignil::Opcode at compile time.
static_assert(static_cast<int>(dragon::ignil::Opcode::COPY)  == DRAGON_IGNIL_COPY);
static_assert(static_cast<int>(dragon::ignil::Opcode::LOAD)  == DRAGON_IGNIL_LOAD);
static_assert(static_cast<int>(dragon::ignil::Opcode::UNDEF) == DRAGON_IGNIL_UNDEF);

const char *dragon_ignil_opcode_name(dragon_ignil_opcode_t op) {
  return dragon::ignil::opcodeName(
             static_cast<dragon::ignil::Opcode>(op))
      .data();
}

// =============================================================================
// IGNIL block — borrowed from graph via reinterpret_cast
// =============================================================================

static const dragon::ignil::Block *
as_ignil_block(const dragon_ignil_block_t *b) {
  return reinterpret_cast<const dragon::ignil::Block *>(b);
}

const dragon_ignil_block_t *
dragon_graph_get_ignil_block(const dragon_graph_t *g, uint64_t addr) {
  const dragon::ignil::Block *b = as_graph(g)->getIGNILBlock(addr);
  return reinterpret_cast<const dragon_ignil_block_t *>(b);
}

size_t dragon_ignil_block_size(const dragon_ignil_block_t *b) {
  return as_ignil_block(b)->size();
}

uint64_t dragon_ignil_block_address(const dragon_ignil_block_t *b) {
  return as_ignil_block(b)->startAddress();
}

char *dragon_ignil_block_to_string(const dragon_ignil_block_t *b) {
  return dup_string(dragon::ignil::toString(*as_ignil_block(b)));
}

char *dragon_ignil_block_op_to_string(const dragon_ignil_block_t *b,
                                      size_t index) {
  const auto &ops = as_ignil_block(b)->ops();
  if (index >= ops.size())
    return nullptr;
  return dup_string(dragon::ignil::toString(ops[index]));
}

void dragon_ignil_block_each_op(
    const dragon_ignil_block_t *b,
    void (*cb)(uint64_t, dragon_ignil_opcode_t, size_t, void *), void *user) {
  const auto &ops = as_ignil_block(b)->ops();
  for (size_t i = 0; i < ops.size(); ++i) {
    cb(ops[i].address,
       static_cast<dragon_ignil_opcode_t>(
           static_cast<int>(ops[i].opcode)),
       i, user);
  }
}

// =============================================================================
// SliceExpr
// =============================================================================

struct dragon_slice_expr {
  std::shared_ptr<dragon::slice::Expr> expr;
};

static dragon_slice_expr_t *
wrap_expr(std::shared_ptr<dragon::slice::Expr> ptr) {
  if (!ptr)
    return nullptr;
  return new dragon_slice_expr{std::move(ptr)};
}

void dragon_slice_expr_destroy(dragon_slice_expr_t *e) { delete e; }

dragon_slice_type_t dragon_slice_expr_type(const dragon_slice_expr_t *e) {
  if (!e)
    return DRAGON_SLICE_UNDEF;
  return static_cast<dragon_slice_type_t>(e->expr->data.index());
}

char *dragon_slice_expr_to_string(const dragon_slice_expr_t *e) {
  if (!e)
    return dup_string("undef");
  return dup_string(dragon::slice::toString(*e->expr));
}

uint64_t dragon_slice_expr_const_value(const dragon_slice_expr_t *e) {
  if (!e)
    return 0;
  const auto *c = std::get_if<dragon::slice::Const>(&e->expr->data);
  return c ? c->value : 0;
}

uint8_t dragon_slice_expr_const_width(const dragon_slice_expr_t *e) {
  if (!e)
    return 0;
  const auto *c = std::get_if<dragon::slice::Const>(&e->expr->data);
  return c ? c->width : 0;
}

uint32_t dragon_slice_expr_symreg_offset(const dragon_slice_expr_t *e) {
  if (!e)
    return 0;
  const auto *r = std::get_if<dragon::slice::SymReg>(&e->expr->data);
  return r ? r->reg.offset : 0;
}

uint8_t dragon_slice_expr_symreg_width(const dragon_slice_expr_t *e) {
  if (!e)
    return 0;
  const auto *r = std::get_if<dragon::slice::SymReg>(&e->expr->data);
  return r ? r->reg.width : 0;
}

const char *dragon_slice_expr_symreg_name(const dragon_slice_expr_t *e) {
  if (!e)
    return "";
  const auto *r = std::get_if<dragon::slice::SymReg>(&e->expr->data);
  return r ? r->reg.name.c_str() : "";
}

dragon_ignil_opcode_t
dragon_slice_expr_opcode(const dragon_slice_expr_t *e) {
  if (!e)
    return DRAGON_IGNIL_UNDEF;
  if (const auto *b = std::get_if<dragon::slice::BinOp>(&e->expr->data))
    return static_cast<dragon_ignil_opcode_t>(static_cast<int>(b->op));
  if (const auto *u = std::get_if<dragon::slice::UnaryOp>(&e->expr->data))
    return static_cast<dragon_ignil_opcode_t>(static_cast<int>(u->op));
  return DRAGON_IGNIL_UNDEF;
}

dragon_slice_expr_t *dragon_slice_expr_left(const dragon_slice_expr_t *e) {
  if (!e)
    return nullptr;
  const auto *b = std::get_if<dragon::slice::BinOp>(&e->expr->data);
  return b ? wrap_expr(b->left) : nullptr;
}

dragon_slice_expr_t *dragon_slice_expr_right(const dragon_slice_expr_t *e) {
  if (!e)
    return nullptr;
  const auto *b = std::get_if<dragon::slice::BinOp>(&e->expr->data);
  return b ? wrap_expr(b->right) : nullptr;
}

dragon_slice_expr_t *dragon_slice_expr_operand(const dragon_slice_expr_t *e) {
  if (!e)
    return nullptr;
  const auto *u = std::get_if<dragon::slice::UnaryOp>(&e->expr->data);
  return u ? wrap_expr(u->operand) : nullptr;
}

dragon_slice_expr_t *
dragon_slice_expr_load_addr(const dragon_slice_expr_t *e) {
  if (!e)
    return nullptr;
  const auto *l = std::get_if<dragon::slice::Load>(&e->expr->data);
  return l ? wrap_expr(l->addr) : nullptr;
}

uint8_t dragon_slice_expr_load_width(const dragon_slice_expr_t *e) {
  if (!e)
    return 0;
  const auto *l = std::get_if<dragon::slice::Load>(&e->expr->data);
  return l ? l->width : 0;
}

#ifdef DRAGON_WITH_Z3
char *dragon_slice_expr_to_z3_string(const dragon_slice_expr_t *e) {
  if (!e)
    return dup_string("undef");
  z3::context ctx;
  dragon::slice::SliceExprZ3 lowerer{ctx};
  return dup_string(lowerer.lower(*e->expr).to_string());
}
#endif

// =============================================================================
// BackwardSlicer
// =============================================================================

struct dragon_slicer {
  dragon::BackwardSlicer slicer;
};

dragon_slicer_t *dragon_slicer_create(int max_blocks) {
  return new dragon_slicer{dragon::BackwardSlicer{max_blocks > 0 ? max_blocks : 5}};
}

// =============================================================================
// LLVMLifter
// =============================================================================

struct dragon_llvm_lifter {
  dragon::llvmlifter::LLVMLifter lifter;
};

dragon_llvm_lifter_t *dragon_llvm_lifter_create(dragon_architecture_t arch,
                                                const char *module_name) {
  std::string name = (module_name && *module_name) ? module_name : "lifted";
  return new dragon_llvm_lifter{
          dragon::llvmlifter::LLVMLifter(to_arch(arch), name)};
}

void dragon_llvm_lifter_destroy(dragon_llvm_lifter_t *l) { delete l; }

int dragon_llvm_lifter_lift(dragon_llvm_lifter_t *l,
                             const dragon_function_t *fn,
                             const dragon_graph_t *g) {
  if (!l || !fn || !g)
    return 0;
  return l->lifter.lift(*as_fn(fn), *as_graph(g)) ? 1 : 0;
}

char *dragon_llvm_lifter_ir(const dragon_llvm_lifter_t *l) {
  if (!l)
    return dup_string("");
  return dup_string(l->lifter.ir());
}

void dragon_llvm_lifter_run_mem2reg(dragon_llvm_lifter_t *l) {
  if (l)
    l->lifter.runMem2Reg();
}

void dragon_llvm_lifter_run_inst_combine(dragon_llvm_lifter_t *l) {
  if (l)
    l->lifter.runInstCombine();
}

void dragon_llvm_lifter_run_gvn(dragon_llvm_lifter_t *l) {
  if (l)
    l->lifter.runGVN();
}

void dragon_llvm_lifter_run_dce(dragon_llvm_lifter_t *l) {
  if (l)
    l->lifter.runDCE();
}

void dragon_slicer_destroy(dragon_slicer_t *s) { delete s; }

dragon_slice_expr_t *dragon_slicer_slice_reg(const dragon_slicer_t *s,
                                              const dragon_graph_t *g,
                                              uint64_t block_addr,
                                              uint32_t reg_offset,
                                              uint8_t reg_width,
                                              const char *reg_name) {
  if (!s || !g)
    return nullptr;
  std::string name = (reg_name && *reg_name) ? reg_name : "";
  dragon::ignil::Value reg{
      dragon::ignil::Reg{reg_offset, reg_width, std::move(name)}};
  dragon::slice::Expr result =
      s->slicer.slice(reg, block_addr, *as_graph(g));
  return new dragon_slice_expr{
      std::make_shared<dragon::slice::Expr>(std::move(result))};
}
