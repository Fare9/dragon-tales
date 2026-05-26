#include <dragon/Assembler.hpp>

#include "ARMAssemblerTarget.hpp"
#include "LLVMAssemblerTarget.hpp"
#include "RISCVAssemblerTarget.hpp"
#include "X86AssemblerTarget.hpp"

#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCParser/MCAsmParser.h>
#include <llvm/MC/MCParser/MCTargetAsmParser.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCTargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>

// -----------------------------------------------------------------------
// Impl — holds the LLVM assembler target state, hidden from the public header
// -----------------------------------------------------------------------

struct dragon::Assembler::Impl {
  LLVMAssemblerTarget target;
};

// -----------------------------------------------------------------------
// Constructor / destructor / move
// -----------------------------------------------------------------------

dragon::Assembler::Assembler(Architecture architecture,
                             const Configuration &config) :
    architecture_(architecture), configuration_(config),
    impl_(std::make_unique<Impl>()) {
  switch (architecture) {
    case Architecture::X86_32:
      impl_->target = internal::initX86Assembler(false);
      break;
    case Architecture::X86_64:
      impl_->target = internal::initX86Assembler(true);
      break;
    case Architecture::ARM32:
      impl_->target = internal::initARMAssembler(false);
      break;
    case Architecture::ARM64:
      impl_->target = internal::initARMAssembler(true);
      break;
    case Architecture::RISCV32:
      impl_->target = internal::initRISCVAssembler(false);
      break;
    case Architecture::RISCV64:
      impl_->target = internal::initRISCVAssembler(true);
      break;
    default:
      break;
  }
}

// These two will need to have the implementation of Pimpl class
// in order to properly release the memory

dragon::Assembler::~Assembler() = default;

dragon::Assembler::Assembler(Assembler &&) noexcept = default;

// -----------------------------------------------------------------------
// isValid
// -----------------------------------------------------------------------

bool dragon::Assembler::isValid() const noexcept {
  return impl_ && impl_->target.instrInfo != nullptr;
}

// -----------------------------------------------------------------------
// extractTextSection — pull raw bytes from ELF .text
//  This is a needed step because LLVM when it generates bytes from
//  assemble instructions, it assembles them into an object file, so
//  we need to extract the bytes from it.
// -----------------------------------------------------------------------

static std::vector<std::uint8_t>
extractTextSection(const llvm::SmallVector<char, 4096> &buffer) {
  auto memBuffer = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(buffer.data(), buffer.size()), "<dragon-elf>", false);

  auto objFileOrErr = llvm::object::ObjectFile::createObjectFile(*memBuffer);
  if (!objFileOrErr) {
    llvm::consumeError(objFileOrErr.takeError());
    return {};
  }

  for (const auto &section: (*objFileOrErr)->sections()) {
    auto nameOrErr = section.getName();
    if (!nameOrErr)
      continue;
    if (*nameOrErr != ".text")
      continue;

    auto contentsOrErr = section.getContents();
    if (!contentsOrErr)
      continue;

    return {contentsOrErr->bytes_begin(), contentsOrErr->bytes_end()};
  }
  return {};
}

// -----------------------------------------------------------------------
// assemble - text → raw bytes via LLVM MC → ELF → .text extraction
// -----------------------------------------------------------------------

std::vector<std::uint8_t>
dragon::Assembler::assemble(std::string_view text) const {
  if (!isValid())
    return {};

  const auto &target = impl_->target;
  const std::string &triple = target.triple;

  std::string error;
  const llvm::Target *llvmTarget =
          llvm::TargetRegistry::lookupTarget(triple, error);
  if (!llvmTarget)
    return {};

  llvm::SmallVector<char, 4096> outputBuffer;
  llvm::raw_svector_ostream outputStream(outputBuffer);

  // Context and objectFileInfo recreated per call — MCContext is stateful
  auto context = std::make_unique<llvm::MCContext>(
          llvm::Triple(triple), target.asmInfo.get(), target.regInfo.get(),
          target.subtargetInfo.get());

  auto objectFileInfo = std::make_unique<llvm::MCObjectFileInfo>();
  objectFileInfo->initMCObjectFileInfo(*context, false);
  context->setObjectFileInfo(objectFileInfo.get());

  // Backend and emitter consumed by streamer — recreate per call
  llvm::MCTargetOptions targetOptions;
  auto backend =
          std::unique_ptr<llvm::MCAsmBackend>(llvmTarget->createMCAsmBackend(
                  *target.subtargetInfo, *target.regInfo, targetOptions));
  if (!backend)
    return {};

  auto emitter = std::unique_ptr<llvm::MCCodeEmitter>(
          llvmTarget->createMCCodeEmitter(*target.instrInfo, *context));
  if (!emitter)
    return {};

  auto objectWriter = backend->createObjectWriter(outputStream);

  auto streamer =
          std::unique_ptr<llvm::MCStreamer>(llvmTarget->createMCObjectStreamer(
                  llvm::Triple(triple), *context, std::move(backend),
                  std::move(objectWriter), std::move(emitter),
                  *target.subtargetInfo,
                  true, // RelaxAll
                  false, // IncrementalLinkerCompatible
                  false // DWARFMustBeAtTheEnd
                  ));
  if (!streamer)
    return {};

  auto srcBuffer = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(text.data(), text.size()), "<dragon-asm>", false);
  llvm::SourceMgr srcMgr;
  srcMgr.AddNewSourceBuffer(std::move(srcBuffer), llvm::SMLoc());

  auto parser = std::unique_ptr<llvm::MCAsmParser>(llvm::createMCAsmParser(
          srcMgr, *context, *streamer, *target.asmInfo));
  if (!parser)
    return {};

  auto targetParser = std::unique_ptr<llvm::MCTargetAsmParser>(
          llvmTarget->createMCAsmParser(*target.subtargetInfo, *parser,
                                        *target.instrInfo, targetOptions));
  if (!targetParser)
    return {};

  parser->setTargetParser(*targetParser);

  if (parser->Run(false))
    return {};

  streamer->finish();

  return extractTextSection(outputBuffer);
}
