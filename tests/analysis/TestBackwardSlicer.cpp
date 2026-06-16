#include <catch2/catch_test_macros.hpp>

#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <iostream>

namespace x86 = dragon::x86;

// ---------------------------------------------------------------------------
// Helper: register an IGNIL block and mark it valid in the graph.
// ---------------------------------------------------------------------------

static void addValidBlock(dragon::Graph &g, dragon::ignil::Block block) {
  std::uint64_t addr = block.startAddress();
  g.addIGNILBlock(std::move(block));
  g.blocks_q.mark_processed(addr);
  g.blocks_q.mark_valid(addr);
}

// ---------------------------------------------------------------------------
// Test 1: constant folded through a single COPY in one block
// ---------------------------------------------------------------------------

TEST_CASE("BackwardSlicer — constant propagation through COPY",
          "[slicer][ignil]") {
  constexpr std::uint64_t baseAddr = 0x1000;
  dragon::ignil::Block block{baseAddr};

  // COPY(rax, 42:i64)  ← simulates  mov rax, 42
  block.append({dragon::ignil::Opcode::COPY,
                dragon::ignil::Value{x86::RAX},
                {dragon::ignil::Value{dragon::ignil::Const{42, 64}}},
                baseAddr});
  block.append({dragon::ignil::Opcode::RET, std::nullopt, {}, baseAddr + 7});

  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  addValidBlock(g, std::move(block));

  dragon::BackwardSlicer slicer;
  dragon::slice::Expr result =
      slicer.slice(dragon::ignil::Value{x86::RAX}, baseAddr, g);

  std::cout << "Slice result: " << dragon::slice::toString(result) << "\n";

  REQUIRE(std::holds_alternative<dragon::slice::Const>(result.data));
  const auto &c = std::get<dragon::slice::Const>(result.data);
  REQUIRE(c.value == 42);
  REQUIRE(c.width == 64);
}

// ---------------------------------------------------------------------------
// Test 2: ADD expression — result is represented as BinOp(ADD, rax, rbx)
// ---------------------------------------------------------------------------

TEST_CASE("BackwardSlicer — ADD produces BinOp expression", "[slicer][ignil]") {
  constexpr std::uint64_t baseAddr = 0x2000;
  dragon::ignil::Block block{baseAddr};

  // t0 = ADD(rax, rbx)
  dragon::ignil::Temp t0{0, 64};
  block.append({dragon::ignil::Opcode::ADD,
                dragon::ignil::Value{t0},
                {dragon::ignil::Value{x86::RAX}, dragon::ignil::Value{x86::RBX}},
                baseAddr});
  // rax = COPY(t0)
  block.append({dragon::ignil::Opcode::COPY,
                dragon::ignil::Value{x86::RAX},
                {dragon::ignil::Value{t0}},
                baseAddr + 3});
  block.append({dragon::ignil::Opcode::RET, std::nullopt, {}, baseAddr + 6});

  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  addValidBlock(g, std::move(block));

  dragon::BackwardSlicer slicer;
  dragon::slice::Expr result =
      slicer.slice(dragon::ignil::Value{x86::RAX}, baseAddr, g);

  std::cout << "Slice result: " << dragon::slice::toString(result) << "\n";

  // Should be BinOp{ADD, SymReg{rax}, SymReg{rbx}}
  REQUIRE(std::holds_alternative<dragon::slice::BinOp>(result.data));
  const auto &b = std::get<dragon::slice::BinOp>(result.data);
  REQUIRE(b.op == dragon::ignil::Opcode::ADD);
  REQUIRE(std::holds_alternative<dragon::slice::SymReg>(b.left->data));
  REQUIRE(std::holds_alternative<dragon::slice::SymReg>(b.right->data));
  REQUIRE(std::get<dragon::slice::SymReg>(b.left->data).reg.offset ==
          x86::RAX.offset);
  REQUIRE(std::get<dragon::slice::SymReg>(b.right->data).reg.offset ==
          x86::RBX.offset);
}

// ---------------------------------------------------------------------------
// Test 3: cross-block propagation — rax defined in predecessor block
// ---------------------------------------------------------------------------

TEST_CASE("BackwardSlicer — cross-block constant propagation",
          "[slicer][ignil]") {
  // Block A (0x3000): COPY(rax, 99); JMP(0x3010)
  // Block B (0x3010): RET            <-- slice rax here
  constexpr std::uint64_t addrA = 0x3000;
  constexpr std::uint64_t addrB = 0x3010;

  dragon::ignil::Block blockA{addrA};
  blockA.append({dragon::ignil::Opcode::COPY,
                 dragon::ignil::Value{x86::RAX},
                 {dragon::ignil::Value{dragon::ignil::Const{99, 64}}},
                 addrA});
  blockA.append({dragon::ignil::Opcode::JMP,
                 std::nullopt,
                 {dragon::ignil::Value{dragon::ignil::Const{addrB, 64}}},
                 addrA + 7});

  dragon::ignil::Block blockB{addrB};
  blockB.append({dragon::ignil::Opcode::RET, std::nullopt, {}, addrB});

  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  addValidBlock(g, std::move(blockA));
  addValidBlock(g, std::move(blockB));

  dragon::BackwardSlicer slicer{5};
  dragon::slice::Expr result =
      slicer.slice(dragon::ignil::Value{x86::RAX}, addrB, g);

  std::cout << "Slice result (cross-block): " << dragon::slice::toString(result)
            << "\n";

  REQUIRE(std::holds_alternative<dragon::slice::Const>(result.data));
  const auto &c = std::get<dragon::slice::Const>(result.data);
  REQUIRE(c.value == 99);
  REQUIRE(c.width == 64);
}

// ---------------------------------------------------------------------------
// Test 4: join node stops propagation — rax left as SymReg
// ---------------------------------------------------------------------------

TEST_CASE("BackwardSlicer — join node stops at SymReg",
          "[slicer][ignil]") {
  // Block A (0x4000): JMP(0x4020)
  // Block B (0x4010): JMP(0x4020)
  // Block C (0x4020): RET  <-- slice rax here; has two predecessors → join
  constexpr std::uint64_t addrA = 0x4000;
  constexpr std::uint64_t addrB = 0x4010;
  constexpr std::uint64_t addrC = 0x4020;

  dragon::ignil::Block blockA{addrA};
  blockA.append({dragon::ignil::Opcode::COPY,
                 dragon::ignil::Value{x86::RAX},
                 {dragon::ignil::Value{dragon::ignil::Const{1, 64}}},
                 addrA});
  blockA.append({dragon::ignil::Opcode::JMP,
                 std::nullopt,
                 {dragon::ignil::Value{dragon::ignil::Const{addrC, 64}}},
                 addrA + 7});

  dragon::ignil::Block blockB{addrB};
  blockB.append({dragon::ignil::Opcode::COPY,
                 dragon::ignil::Value{x86::RAX},
                 {dragon::ignil::Value{dragon::ignil::Const{2, 64}}},
                 addrB});
  blockB.append({dragon::ignil::Opcode::JMP,
                 std::nullopt,
                 {dragon::ignil::Value{dragon::ignil::Const{addrC, 64}}},
                 addrB + 7});

  dragon::ignil::Block blockC{addrC};
  blockC.append({dragon::ignil::Opcode::RET, std::nullopt, {}, addrC});

  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  addValidBlock(g, std::move(blockA));
  addValidBlock(g, std::move(blockB));
  addValidBlock(g, std::move(blockC));

  dragon::BackwardSlicer slicer{5};
  dragon::slice::Expr result =
      slicer.slice(dragon::ignil::Value{x86::RAX}, addrC, g);

  std::cout << "Slice result (join): " << dragon::slice::toString(result) << "\n";

  // C has two predecessors → join node → SymReg, not a concrete constant.
  REQUIRE(std::holds_alternative<dragon::slice::SymReg>(result.data));
  REQUIRE(std::get<dragon::slice::SymReg>(result.data).reg.offset ==
          x86::RAX.offset);
}
