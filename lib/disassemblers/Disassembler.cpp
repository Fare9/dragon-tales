#include <dragon/Disassembler.hpp>

#include <stack>
#include <unordered_set>

#include "InstructionImpl.hpp"
#include "disassemblers/ARMTarget.hpp"
#include "disassemblers/LLVMTarget.hpp"
#include "disassemblers/RISCVTarget.hpp"
#include "disassemblers/X86Target.hpp"
#include "disassemblers/x86/x86Analysis.hpp"
#include "utils/Logger.hpp"

struct dragon::Disassembler::Impl {
  LLVMTarget target;
  internal::Logger logger;
};

dragon::Disassembler::Disassembler(Architecture architecture,
                                   const Configuration &configuration) :
    architecture_(architecture), configuration_(configuration),
    impl_(std::make_unique<Impl>()) {
  impl_->logger = internal::Logger{configuration_.logging};

  const unsigned syntaxVariant =
          (configuration_.syntax == Syntax::INTEL) ? 1 : 0;

  const std::string &cpu = configuration_.cpu;
  const std::string &features = configuration_.features;

  switch (architecture) {
    case Architecture::X86_32:
      impl_->target = internal::initX86(false, syntaxVariant, cpu, features);
      break;
    case Architecture::X86_64:
      impl_->target = internal::initX86(true, syntaxVariant, cpu, features);
      break;
    case Architecture::ARM32:
      impl_->target = internal::initARM(false, cpu, features);
      break;
    case Architecture::ARM64:
      impl_->target = internal::initARM(true, cpu, features);
      break;
    case Architecture::RISCV32:
      impl_->target = internal::initRISCV(false, cpu, features);
      break;
    case Architecture::RISCV64:
      impl_->target = internal::initRISCV(true, cpu, features);
      break;
    default:
      break;
  }

  impl_->logger.log("Disassembler: arch={} syntax={} strategy={}",
                    ArchitectureName(architecture_),
                    configuration_.syntax == Syntax::INTEL ? "intel" : "att",
                    configuration_.disassemblyStrategy ==
                                    DisassemblyStrategy::LINEAR_SWEEP
                            ? "linear_sweep"
                            : "recursive_traversal");
  if (!isValid())
    impl_->logger.log("Disassembler: target initialization failed for {}",
                      ArchitectureName(architecture_));
}

dragon::Disassembler::~Disassembler() = default;
dragon::Disassembler::Disassembler(Disassembler &&) noexcept = default;

bool dragon::Disassembler::isValid() const noexcept {
  return impl_ && impl_->target.disassembler != nullptr;
}

std::vector<dragon::Instruction>
dragon::Disassembler::disassemble(std::span<const std::uint8_t> bytes,
                                  std::uint64_t baseAddress) const {
  if (!isValid()) {
    impl_->logger.log("disassemble: called on invalid disassembler");
    return {};
  }
  // disassemble is just a wrapper over different disassembly algorithms
  switch (configuration_.disassemblyStrategy) {
    case DisassemblyStrategy::LINEAR_SWEEP:
      return linearSweep(bytes, baseAddress);
    case DisassemblyStrategy::RECURSIVE_TRAVERSAL:
      return recursiveTraversal(bytes, baseAddress);
  }
  return {};
}

void dragon::Disassembler::disassemble(std::span<const std::uint8_t> bytes,
                                       std::uint64_t baseAddress,
                                       Graph &graph) const {
  if (!isValid()) {
    impl_->logger.log("disassemble: called on invalid disassembler");
    return;
  }

  switch (architecture_) {
    case Architecture::X86_32:
    case Architecture::X86_64: {
      x86::X86Analysis x86Analysis{architecture_, *this, configuration_};
      x86Analysis.analyze(bytes, baseAddress, graph);
      break;
    }
    case Architecture::ARM32:
    case Architecture::ARM64:
    case Architecture::RISCV32:
    case Architecture::RISCV64:
      std::cerr << "Not implemented yet\n";
      break;
    case Architecture::Unknown:
      break;
  }
}

dragon::Instruction
dragon::Disassembler::decodeOne(std::span<const std::uint8_t> bytes,
                                std::uint64_t address,
                                std::size_t &offset) const {
  const auto &target = impl_->target;

  llvm::MCInst inst;
  std::uint64_t instSize{0};

  const auto status = target.disassembler->getInstruction(
          inst, instSize,
          llvm::ArrayRef(bytes.data() + offset, bytes.size() - offset),
          address + offset, llvm::nulls());

  if (status != llvm::MCDisassembler::Success || instSize == 0) {
    impl_->logger.log("decodeOne: failed at 0x{:x}, skipping byte",
                      address + offset);
    offset += 1;
    return {address + offset,
            0,
            "",
            "",
            {},
            false,
            false,
            false,
            false,
            false,
            false};
  }

  std::string mnemonic = target.printer->getMnemonic(&inst).first;
  mnemonic.erase(
          std::find_if(mnemonic.rbegin(), mnemonic.rend(),
                       [](const unsigned char c) { return !std::isspace(c); })
                  .base(),
          mnemonic.end());

  std::string full;
  llvm::raw_string_ostream stream(full);
  target.printer->printInst(&inst, address + offset, "", *target.subtargetInfo,
                            stream);
  std::string operands;
  auto mnemonicStart = full.find(mnemonic);
  if (mnemonicStart != std::string::npos)
    operands = full.substr(mnemonicStart + mnemonic.size());
  operands.erase(0, operands.find_first_not_of(" \t"));

  std::vector<std::uint8_t> rawBytes(bytes.begin() + offset,
                                     bytes.begin() + offset + instSize);

  const auto &desc = target.instrInfo->get(inst.getOpcode());
  bool isJump = desc.isBranch();
  bool isCall = desc.isCall();
  bool isReturn = desc.isReturn();
  bool isConditional = desc.isConditionalBranch();
  bool isTrap = desc.isTrap();
  bool hasIndirect = desc.isIndirectBranch();

  offset += instSize;

  Instruction insn(address + offset - instSize,
                   static_cast<std::uint8_t>(instSize), std::move(mnemonic),
                   std::move(operands), std::move(rawBytes), isReturn, isCall,
                   isJump, isConditional, isTrap, hasIndirect);
  // We keep the LLVM object just in case we are going to use it later.
  insn.impl_ = std::make_unique<Instruction::Impl>(Instruction::Impl{inst});

  // Resolve direct branch target from immediate operand
  if (isJump && !hasIndirect && inst.getNumOperands() > 0) {
    const auto &op = inst.getOperand(0);
    if (op.isImm()) {
      insn.addBranch(insn.address() + instSize +
                     static_cast<std::uint64_t>(op.getImm()));
    }
  }

  // If it is a call, we need to calculate possible callees
  if (isCall && !hasIndirect && inst.getNumOperands() > 0) {
    const auto &op = inst.getOperand(0);
    if (op.isImm()) {
      insn.addCallee(insn.address() + instSize +
                     static_cast<std::uint64_t>(op.getImm()));
    }
  }

  // Edge count: known targets, or 1 for fall-through on non-terminator
  std::size_t edgeCount = insn.branches().size();
  if (edgeCount == 0 && !isReturn && !isTrap)
    edgeCount = 1;
  insn.setEdges(edgeCount);

  return insn;
}

std::vector<dragon::Instruction>
dragon::Disassembler::linearSweep(std::span<const std::uint8_t> bytes,
                                  std::uint64_t baseAddress) const {
  impl_->logger.log("LinearSweep: {} bytes at 0x{:x}", bytes.size(),
                    baseAddress);
  std::vector<Instruction> instructions;
  std::size_t offset{0};
  while (offset < bytes.size()) {
    auto instr = decodeOne(bytes, baseAddress, offset);
    if (instr.size() == 0)
      continue;
    instructions.emplace_back(std::move(instr));
  }
  impl_->logger.log("LinearSweep: decoded {} instructions",
                    instructions.size());
  return instructions;
}

std::vector<dragon::Instruction>
dragon::Disassembler::recursiveTraversal(std::span<const std::uint8_t> bytes,
                                         std::uint64_t baseAddress) const {
  impl_->logger.log("RecursiveTraversal: {} bytes at 0x{:x}", bytes.size(),
                    baseAddress);
  std::vector<Instruction> instructions;
  std::unordered_set<std::uint64_t> visited;
  std::stack<std::uint64_t> workList;

  workList.push(baseAddress);

  while (!workList.empty()) {
    const std::uint64_t address = workList.top();
    workList.pop();

    if (visited.contains(address))
      continue;
    if (address < baseAddress)
      continue;

    const std::size_t offsetVal = address - baseAddress;
    if (offsetVal >= bytes.size())
      continue;

    std::size_t offset = offsetVal;
    while (offset < bytes.size()) {
      const std::uint64_t instrAddress = baseAddress + offset;
      if (visited.contains(instrAddress))
        break;

      auto instr = decodeOne(bytes, baseAddress, offset);
      if (instr.size() == 0)
        break;

      visited.insert(instrAddress);

      // Push all known branch targets onto the worklist
      for (std::uint64_t target: instr.branches())
        workList.push(target);

      const bool isTerminator =
              instr.isReturn() ||
              (instr.isJump() && instr.branches().empty()); // indirect jump

      instructions.push_back(std::move(instr));
      if (isTerminator)
        break;
    }
  }

  std::ranges::sort(instructions,
                    [](const Instruction &a, const Instruction &b) {
                      return a.address() < b.address();
                    });

  impl_->logger.log("RecursiveTraversal: decoded {} instructions",
                    instructions.size());
  return instructions;
}
