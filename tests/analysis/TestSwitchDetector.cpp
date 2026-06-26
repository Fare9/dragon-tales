#include <catch2/catch_test_macros.hpp>

#include <dragon/Architecture.hpp>
#include <dragon/Configuration.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/analysis/SwitchDetector.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/disassembler/Instruction.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <cstring>

// Namespace aliases to disambiguate slice::Const from ignil::Const.
namespace slice = dragon::slice;
namespace ignil = dragon::ignil;
using namespace dragon::slice;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dragon::Graph emptyGraph() {
  return dragon::Graph{dragon::Architecture::X86_64, dragon::Configuration{}};
}

// Build LOAD(ADD(Const{base}, MUL(SymReg{reg}, Const{scale})))
static Expr makeJumpTableExpr(std::uint64_t base, const ignil::Reg &reg,
                               std::uint64_t scale) {
  auto baseE  = std::make_shared<Expr>(slice::Const{base, 64});
  auto idxE   = std::make_shared<Expr>(SymReg{reg});
  auto scaleE = std::make_shared<Expr>(slice::Const{scale, 64});
  auto mul    = std::make_shared<Expr>(BinOp{ignil::Opcode::MUL, idxE, scaleE});
  auto add    = std::make_shared<Expr>(BinOp{ignil::Opcode::ADD, baseE, mul});
  return Expr{Load{add, 64}};
}

// ---------------------------------------------------------------------------
// Phase 1 - structural pattern matching (empty graph, no bounds extraction)
// ---------------------------------------------------------------------------

TEST_CASE("SwitchDetector -- detects 8-byte jump-table pattern", "[switch]") {
  auto g  = emptyGraph();
  ignil::Reg rax{0, 64, "RAX"};
  Expr expr = makeJumpTableExpr(0x4000, rax, 8);

  auto sw = detectPossibleSwitch(expr, 0x2000, g);

  REQUIRE(sw.has_value());
  CHECK(sw->tableBase == 0x4000);
  CHECK(sw->entrySize == 8);
  CHECK(sw->numCases == 0); // empty graph → no predecessor found
  CHECK(sw->indexReg.offset == rax.offset);
  CHECK(sw->indexReg.width == rax.width);
}

TEST_CASE("SwitchDetector -- detects 4-byte jump-table pattern", "[switch]") {
  auto g  = emptyGraph();
  ignil::Reg rcx{8, 64, "RCX"};
  Expr expr = makeJumpTableExpr(0x8000, rcx, 4);

  auto sw = detectPossibleSwitch(expr, 0x3000, g);

  REQUIRE(sw.has_value());
  CHECK(sw->tableBase == 0x8000);
  CHECK(sw->entrySize == 4);
  CHECK(sw->indexReg.offset == rcx.offset);
}

TEST_CASE("SwitchDetector -- commutative outer ADD (MUL before base)", "[switch]") {
  // ADD(MUL(idx, scale), base) - base is the right operand
  auto g   = emptyGraph();
  ignil::Reg rax{0, 64, "RAX"};
  auto baseE  = std::make_shared<Expr>(slice::Const{0x5000, 64});
  auto idxE   = std::make_shared<Expr>(SymReg{rax});
  auto scaleE = std::make_shared<Expr>(slice::Const{8, 64});
  auto mul    = std::make_shared<Expr>(BinOp{ignil::Opcode::MUL, idxE, scaleE});
  Expr expr{Load{std::make_shared<Expr>(BinOp{ignil::Opcode::ADD, mul, baseE}), 64}};

  auto sw = detectPossibleSwitch(expr, 0x1000, g);

  REQUIRE(sw.has_value());
  CHECK(sw->tableBase == 0x5000);
  CHECK(sw->entrySize == 8);
}

TEST_CASE("SwitchDetector -- commutative inner MUL (scale before index)", "[switch]") {
  // LOAD(ADD(base, MUL(scale, idx))) - scale is the left MUL operand
  auto g   = emptyGraph();
  ignil::Reg rdx{24, 64, "RDX"};
  auto baseE  = std::make_shared<Expr>(slice::Const{0x6000, 64});
  auto idxE   = std::make_shared<Expr>(SymReg{rdx});
  auto scaleE = std::make_shared<Expr>(slice::Const{8, 64});
  auto mul    = std::make_shared<Expr>(BinOp{ignil::Opcode::MUL, scaleE, idxE});
  auto add    = std::make_shared<Expr>(BinOp{ignil::Opcode::ADD, baseE, mul});
  Expr expr{Load{add, 64}};

  auto sw = detectPossibleSwitch(expr, 0x1000, g);

  REQUIRE(sw.has_value());
  CHECK(sw->tableBase == 0x6000);
  CHECK(sw->indexReg.offset == rdx.offset);
}

TEST_CASE("SwitchDetector -- rejects plain register target", "[switch]") {
  auto g = emptyGraph();
  Expr expr{SymReg{ignil::Reg{0, 64, "RAX"}}};
  CHECK_FALSE(detectPossibleSwitch(expr, 0x1000, g).has_value());
}

TEST_CASE("SwitchDetector -- rejects constant target", "[switch]") {
  auto g = emptyGraph();
  Expr expr{slice::Const{0x4000, 64}};
  CHECK_FALSE(detectPossibleSwitch(expr, 0x1000, g).has_value());
}

TEST_CASE("SwitchDetector -- rejects wrong scale (3 bytes)", "[switch]") {
  auto g = emptyGraph();
  Expr expr = makeJumpTableExpr(0x4000, ignil::Reg{0, 64, "RAX"}, 3);
  CHECK_FALSE(detectPossibleSwitch(expr, 0x1000, g).has_value());
}

TEST_CASE("SwitchDetector -- rejects ADD without Load wrapper", "[switch]") {
  auto g      = emptyGraph();
  auto baseE  = std::make_shared<Expr>(slice::Const{0x4000, 64});
  auto idxE   = std::make_shared<Expr>(SymReg{ignil::Reg{0, 64, "RAX"}});
  auto scaleE = std::make_shared<Expr>(slice::Const{8, 64});
  auto mul    = std::make_shared<Expr>(BinOp{ignil::Opcode::MUL, idxE, scaleE});
  Expr expr{BinOp{ignil::Opcode::ADD, baseE, mul}};
  CHECK_FALSE(detectPossibleSwitch(expr, 0x1000, g).has_value());
}

// ---------------------------------------------------------------------------
// Phase 2 - bounds extraction from a CJMP predecessor block
// ---------------------------------------------------------------------------

TEST_CASE("SwitchDetector -- extracts numCases from UGE predecessor",
          "[switch]") {
  dragon::Configuration cfg;
  dragon::Graph g{dragon::Architecture::X86_64, cfg};

  // IGNIL predecessor block at 0x1000:
  //   t0 = UGE(RAX, 5)           ; idx >= 5 means out-of-bounds
  //   CJMP(t0, 0x3000, 0x1005)   ; taken=default, not-taken=switch block
  ignil::Block predBlock{0x1000};
  predBlock.append({ignil::Opcode::UGE,
                    ignil::Value{ignil::Temp{0, 1}},
                    {ignil::Value{ignil::Reg{0, 64, "RAX"}},
                     ignil::Value{ignil::Const{5, 64}}},
                    0x1000});
  predBlock.append({ignil::Opcode::CJMP,
                    std::nullopt,
                    {ignil::Value{ignil::Temp{0, 1}},
                     ignil::Value{ignil::Const{0x3000, 64}},
                     ignil::Value{ignil::Const{0x1005, 64}}},
                    0x1003});

  // Instruction at 0x1000: JAE to default, size 5 so fall-through = 0x1005
  dragon::Instruction predInsn{
      0x1000, 5, "jae", "0x3000",
      std::vector<std::uint8_t>{0x0F, 0x83, 0x00, 0x00, 0x00},
      false, false, true, true, false, false};
  predInsn.addBranch(0x3000);
  predInsn.setBlockStart(true);

  g.addInstruction(std::move(predInsn));
  g.addBlock(0x1000);
  g.addIGNILBlock(std::move(predBlock));

  ignil::Reg rax{0, 64, "RAX"};
  Expr jumpTarget = makeJumpTableExpr(0x4000, rax, 8);

  auto sw = detectPossibleSwitch(jumpTarget, 0x1005, g);

  REQUIRE(sw.has_value());
  CHECK(sw->tableBase == 0x4000);
  CHECK(sw->entrySize == 8);
  CHECK(sw->numCases == 5);
  CHECK(sw->indexReg.offset == rax.offset);
}

// ---------------------------------------------------------------------------
// readTableTargets
// ---------------------------------------------------------------------------

TEST_CASE("readTableTargets -- returns empty when numCases is 0", "[switch]") {
  SwitchCandidate sw{0x4000, 8, 0, ignil::Reg{0, 64, "RAX"}};
  std::vector<std::uint8_t> bytes(64, 0);
  CHECK(readTableTargets(sw, bytes, 0x4000).empty());
}

TEST_CASE("readTableTargets -- returns empty when table base is before buffer",
          "[switch]") {
  SwitchCandidate sw{0x3F00, 8, 4, ignil::Reg{0, 64, "RAX"}};
  std::vector<std::uint8_t> bytes(64, 0); // covers 0x4000 - 0x403F
  CHECK(readTableTargets(sw, bytes, 0x4000).empty());
}

TEST_CASE("readTableTargets -- returns empty when table extends beyond buffer",
          "[switch]") {
  // Table at 0x4038 (offset 56), 4 x 8 = 32 bytes needed, only 8 remain
  SwitchCandidate sw{0x4038, 8, 4, ignil::Reg{0, 64, "RAX"}};
  std::vector<std::uint8_t> bytes(64, 0);
  CHECK(readTableTargets(sw, bytes, 0x4000).empty());
}

TEST_CASE("readTableTargets -- reads 8-byte entries correctly", "[switch]") {
  // Buffer at 0x2000, jump table at 0x2010, three 64-bit entries
  std::vector<std::uint8_t> bytes(0x40, 0);
  const std::uint64_t t0 = 0x3000, t1 = 0x3010, t2 = 0x3020;
  std::memcpy(bytes.data() + 0x10, &t0, 8);
  std::memcpy(bytes.data() + 0x18, &t1, 8);
  std::memcpy(bytes.data() + 0x20, &t2, 8);

  SwitchCandidate sw{0x2010, 8, 3, ignil::Reg{0, 64, "RAX"}};
  auto targets = readTableTargets(sw, bytes, 0x2000);

  REQUIRE(targets.size() == 3);
  CHECK(targets[0] == 0x3000);
  CHECK(targets[1] == 0x3010);
  CHECK(targets[2] == 0x3020);
}

TEST_CASE("readTableTargets -- reads 4-byte entries correctly", "[switch]") {
  // Buffer at 0x3000, jump table at 0x3008, four 32-bit entries
  std::vector<std::uint8_t> bytes(0x40, 0);
  const std::uint32_t t0 = 0x1000, t1 = 0x1010, t2 = 0x1020, t3 = 0x1030;
  std::memcpy(bytes.data() + 0x08, &t0, 4);
  std::memcpy(bytes.data() + 0x0C, &t1, 4);
  std::memcpy(bytes.data() + 0x10, &t2, 4);
  std::memcpy(bytes.data() + 0x14, &t3, 4);

  SwitchCandidate sw{0x3008, 4, 4, ignil::Reg{0, 64, "RDX"}};
  auto targets = readTableTargets(sw, bytes, 0x3000);

  REQUIRE(targets.size() == 4);
  CHECK(targets[0] == 0x1000);
  CHECK(targets[1] == 0x1010);
  CHECK(targets[2] == 0x1020);
  CHECK(targets[3] == 0x1030);
}
