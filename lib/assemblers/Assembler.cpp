#include <dragon/assembler/Assembler.hpp>

#include "assemblers/ARMAssemblerTarget.hpp"
#include "assemblers/LLVMAssemblerTarget.hpp"
#include "assemblers/RISCVAssemblerTarget.hpp"
#include "assemblers/X86AssemblerTarget.hpp"
#include "utils/Logger.hpp"

#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCAssembler.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCObjectStreamer.h>
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
// Impl - holds the LLVM assembler target state, hidden from the public header
// -----------------------------------------------------------------------

/// The Impl for Assembler contains the objects to use LLVM
/// for the assembly process of instructions, as stated in
/// the documentation, context, backend, and emitter are
/// recreated per assemble()
struct dragon::Assembler::Impl {
  LLVMAssemblerTarget target; ///< LLVM objects to create the assemblers.
  internal::Logger logger;
};

// -----------------------------------------------------------------------
// Constructor / destructor / move
// -----------------------------------------------------------------------

dragon::Assembler::Assembler(Architecture architecture,
                             const Configuration &config) :
    architecture_(architecture), configuration_(config),
    impl_(std::make_unique<Impl>()) {
  impl_->logger = internal::Logger{configuration_.logging};

  const std::string &cpu      = configuration_.cpu;
  const std::string &features = configuration_.features;

  switch (architecture) {
    case Architecture::X86_32:
      impl_->target = internal::initX86Assembler(false, cpu, features);
      break;
    case Architecture::X86_64:
      impl_->target = internal::initX86Assembler(true, cpu, features);
      break;
    case Architecture::ARM32:
      impl_->target = internal::initARMAssembler(false, cpu, features);
      break;
    case Architecture::ARM64:
      impl_->target = internal::initARMAssembler(true, cpu, features);
      break;
    case Architecture::RISCV32:
      impl_->target = internal::initRISCVAssembler(false, cpu, features);
      break;
    case Architecture::RISCV64:
      impl_->target = internal::initRISCVAssembler(true, cpu, features);
      break;
    default:
      break;
  }

  impl_->logger.log("Assembler: arch={} syntax={}", ArchitectureName(architecture_),
                    configuration_.syntax == Syntax::INTEL ? "intel" : "att");
  if (!isValid())
    impl_->logger.log("Assembler: target initialization failed for {}",
                      ArchitectureName(architecture_));
}

// These two will need to have the implementation of Pimpl class
// in order to properly release the memory

dragon::Assembler::~Assembler() = default;

dragon::Assembler::Assembler(Assembler &&) noexcept = default;

// -----------------------------------------------------------------------
// isValid
// -----------------------------------------------------------------------

bool dragon::Assembler::isValid() const noexcept {
  // Check minimum information to know if the assembler
  // has been created properly.
  return impl_ && impl_->target.instrInfo != nullptr;
}

// -----------------------------------------------------------------------
// extractTextSection - pull raw bytes from ELF .text
//  This is a needed step because LLVM when it generates bytes from
//  assemble instructions, it assembles them into an object file, so
//  we need to extract the bytes from it.
// -----------------------------------------------------------------------

static std::vector<std::uint8_t>
extractTextSection(const llvm::SmallVector<char, 4096> &buffer) {
  // generate a memory buffer object from the provided data, in that
  // way, once we have worked with it, we don't need to care about
  // memory manipulation
  auto memBuffer = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(buffer.data(), buffer.size()), "<dragon-elf>", false);
  // ObjectFile is an agnostic representation of different object files
  // in our case, here it will be an ELF file created by the assembler
  // when assembling instructions
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
    // Return the content of the section as a vector of bytes.
    return {contentsOrErr->bytes_begin(), contentsOrErr->bytes_end()};
  }
  return {};
}

// -----------------------------------------------------------------------
// assemble - text → raw bytes via LLVM MC → ELF → .text extraction
// -----------------------------------------------------------------------

std::vector<std::uint8_t>
dragon::Assembler::assemble(std::string_view text) const {
  if (!isValid()) {
    impl_->logger.log("assemble: called on invalid assembler");
    return {};
  }
  impl_->logger.log("assemble: {} chars of source text", text.size());

  // For x86, prepend the Intel-syntax directive so the user doesn't have to.
  // AT&T is the GNU assembler default, so no prefix is needed for ATT.
  std::string patchedSource;
  const bool isX86 = architecture_ == Architecture::X86_32 ||
                     architecture_ == Architecture::X86_64;
  if (isX86 && configuration_.syntax == Syntax::INTEL)
    patchedSource = ".intel_syntax noprefix\n";
  patchedSource += text;
  // All the LLVM objects depending on the architecture
  const auto &target = impl_->target;
  const std::string &triple = target.triple;

  std::string error;
  const llvm::Target *llvmTarget =
          llvm::TargetRegistry::lookupTarget(triple, error);
  if (!llvmTarget) {
    impl_->logger.log("assemble: target lookup failed for triple '{}'", triple);
    return {};
  }
  // Buffer and stream where the assembled code will be written.
  llvm::SmallVector<char, 4096> outputBuffer;
  llvm::raw_svector_ostream outputStream(outputBuffer);

  // Context and objectFileInfo recreated per call - MCContext is stateful
  auto context = std::make_unique<llvm::MCContext>(
          llvm::Triple(triple), target.asmInfo.get(), target.regInfo.get(),
          target.subtargetInfo.get());

  auto objectFileInfo = std::make_unique<llvm::MCObjectFileInfo>();
  objectFileInfo->initMCObjectFileInfo(*context, false);
  context->setObjectFileInfo(objectFileInfo.get());

  // Backend and emitter consumed by streamer - recreate per call
  llvm::MCTargetOptions targetOptions;
  auto backend =
          std::unique_ptr<llvm::MCAsmBackend>(llvmTarget->createMCAsmBackend(
                  *target.subtargetInfo, *target.regInfo, targetOptions));
  if (!backend) {
    impl_->logger.log("assemble: failed to create backend");
    return {};
  }

  auto emitter = std::unique_ptr<llvm::MCCodeEmitter>(
          llvmTarget->createMCCodeEmitter(*target.instrInfo, *context));
  if (!emitter) {
    impl_->logger.log("assemble: failed to create code emitter");
    return {};
  }

  auto objectWriter = backend->createObjectWriter(outputStream);

  auto streamer =
          std::unique_ptr<llvm::MCStreamer>(llvmTarget->createMCObjectStreamer(
                  llvm::Triple(triple), *context, std::move(backend),
                  std::move(objectWriter), std::move(emitter),
                  *target.subtargetInfo));
  if (!streamer) {
    impl_->logger.log("assemble: failed to create streamer");
    return {};
  }
  static_cast<llvm::MCObjectStreamer *>(streamer.get())
          ->getAssembler()
          .setRelaxAll(true);

  auto srcBuffer = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef(patchedSource.data(), patchedSource.size()),
          "<dragon-asm>", false);
  llvm::SourceMgr srcMgr;
  srcMgr.AddNewSourceBuffer(std::move(srcBuffer), llvm::SMLoc());
  // Parser that will be in charge of working with user provided input
  // backend, emitter, streamer are consumed, and cannot be used anymore
  // after the run call.
  auto parser = std::unique_ptr<llvm::MCAsmParser>(llvm::createMCAsmParser(
          srcMgr, *context, *streamer, *target.asmInfo));
  if (!parser) {
    impl_->logger.log("assemble: failed to create parser");
    return {};
  }

  auto targetParser = std::unique_ptr<llvm::MCTargetAsmParser>(
          llvmTarget->createMCAsmParser(*target.subtargetInfo, *parser,
                                        *target.instrInfo, targetOptions));
  if (!targetParser) {
    impl_->logger.log("assemble: failed to create target parser");
    return {};
  }

  parser->setTargetParser(*targetParser);

  if (parser->Run(false)) {
    impl_->logger.log("assemble: parse/assemble failed");
    return {};
  }

  streamer->finish();

  auto result = extractTextSection(outputBuffer);
  if (result.empty())
    impl_->logger.log("assemble: text section extraction failed");
  else
    impl_->logger.log("assemble: produced {} bytes", result.size());
  return result;
}
