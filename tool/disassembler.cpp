#include <dragon/Architecture.hpp>
#include <dragon/Configuration.hpp>
#include <dragon/disassembler/BasicBlock.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/disassembler/Instruction.hpp>

#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/TargetParser/Triple.h>

#include <cstdint>
#include <format>
#include <iostream>
#include <span>
#include <string>

static dragon::Architecture archFromTriple(const llvm::Triple &triple) {
  switch (triple.getArch()) {
    case llvm::Triple::x86:
      return dragon::Architecture::X86_32;
    case llvm::Triple::x86_64:
      return dragon::Architecture::X86_64;
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      return dragon::Architecture::ARM32;
    case llvm::Triple::aarch64:
      return dragon::Architecture::ARM64;
    case llvm::Triple::riscv32:
      return dragon::Architecture::RISCV32;
    case llvm::Triple::riscv64:
      return dragon::Architecture::RISCV64;
    default:
      return dragon::Architecture::Unknown;
  }
}

static dragon::Architecture archFromString(const std::string &s) {
  if (s == "x86_32" || s == "i386")
    return dragon::Architecture::X86_32;
  if (s == "x86_64" || s == "x86-64")
    return dragon::Architecture::X86_64;
  if (s == "arm32" || s == "arm")
    return dragon::Architecture::ARM32;
  if (s == "arm64" || s == "aarch64")
    return dragon::Architecture::ARM64;
  if (s == "riscv32")
    return dragon::Architecture::RISCV32;
  if (s == "riscv64")
    return dragon::Architecture::RISCV64;
  return dragon::Architecture::Unknown;
}

struct Options {
  std::string filePath;
  dragon::Architecture archOverride = dragon::Architecture::Unknown;
  dragon::DisassemblyStrategy strategy =
          dragon::DisassemblyStrategy::LINEAR_SWEEP;
  dragon::Syntax syntax = dragon::Syntax::INTEL;
  std::string sectionName = ".text";
};

static void printUsage(const char *prog) {
  std::cerr << "Usage: " << prog << " <binary> [options]\n"
            << "  --arch <arch>      Architecture override\n"
            << "                     Values: x86_32, x86_64, arm32, arm64, "
               "riscv32, riscv64\n"
            << "  --strategy <s>     Disassembly strategy: linear (default), "
               "recursive\n"
            << "  --syntax <s>       Output syntax: intel (default), att\n"
            << "  --section <name>   Section name (default: .text)\n";
}

static Options parseArgs(int argc, char **argv) {
  if (argc < 2) {
    printUsage(argv[0]);
    std::exit(1);
  }
  Options opts;
  opts.filePath = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--arch") && i + 1 < argc) {
      opts.archOverride = archFromString(argv[++i]);
      if (opts.archOverride == dragon::Architecture::Unknown) {
        std::cerr << "Unknown architecture: " << argv[i] << '\n';
        std::exit(1);
      }
    } else if ((arg == "--strategy") && i + 1 < argc) {
      std::string s = argv[++i];
      if (s == "linear")
        opts.strategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
      else if (s == "recursive")
        opts.strategy = dragon::DisassemblyStrategy::RECURSIVE_TRAVERSAL;
      else {
        std::cerr << "Unknown strategy: " << s << '\n';
        std::exit(1);
      }
    } else if ((arg == "--syntax") && i + 1 < argc) {
      std::string s = argv[++i];
      if (s == "att")
        opts.syntax = dragon::Syntax::ATT;
      else if (s != "intel") {
        std::cerr << "Unknown syntax: " << s << '\n';
        std::exit(1);
      }
    } else if ((arg == "--section") && i + 1 < argc) {
      opts.sectionName = argv[++i];
    } else {
      std::cerr << "Unknown option: " << arg << '\n';
      printUsage(argv[0]);
      std::exit(1);
    }
  }
  return opts;
}

static void listSections(const llvm::object::ObjectFile &obj) {
  std::cerr << "Available sections:\n";
  for (const llvm::object::SectionRef &sec: obj.sections()) {
    auto nameOrErr = sec.getName();
    if (!nameOrErr)
      continue;
    std::cerr << std::format("  {:20s}  addr=0x{:x}  size={}\n",
                             nameOrErr->str(), sec.getAddress(), sec.getSize());
  }
}

int main(int argc, char **argv) {
  Options opts = parseArgs(argc, argv);

  auto bufOrErr = llvm::MemoryBuffer::getFile(opts.filePath);
  if (!bufOrErr) {
    std::cerr << "Cannot open '" << opts.filePath
              << "': " << bufOrErr.getError().message() << '\n';
    return 1;
  }

  auto objOrErr = llvm::object::ObjectFile::createObjectFile(**bufOrErr);
  if (!objOrErr) {
    llvm::logAllUnhandledErrors(objOrErr.takeError(), llvm::errs(),
                                "Error loading object: ");
    return 1;
  }
  const llvm::object::ObjectFile &obj = **objOrErr;

  // Resolve architecture
  dragon::Architecture arch = opts.archOverride;
  if (arch == dragon::Architecture::Unknown) {
    llvm::Triple triple = obj.makeTriple();
    arch = archFromTriple(triple);
    if (arch == dragon::Architecture::Unknown) {
      std::cerr << "Unrecognised architecture in binary: " << triple.str()
                << "\nUse --arch to override.\n";
      return 1;
    }
    std::cout << std::format("Architecture: {} (from binary triple: {})\n",
                             dragon::ArchitectureName(arch), triple.str());
  } else {
    std::cout << std::format("Architecture: {} (user override)\n",
                             dragon::ArchitectureName(arch));
  }

  // Locate the target section
  const std::uint8_t *sectionData = nullptr;
  std::uint64_t sectionAddress = 0;
  std::size_t sectionSize = 0;

  for (const llvm::object::SectionRef &sec: obj.sections()) {
    auto nameOrErr = sec.getName();
    if (!nameOrErr || *nameOrErr != opts.sectionName)
      continue;
    auto contentsOrErr = sec.getContents();
    if (!contentsOrErr)
      continue;
    sectionData = reinterpret_cast<const std::uint8_t *>(contentsOrErr->data());
    sectionSize = contentsOrErr->size();
    sectionAddress = sec.getAddress();
    break;
  }

  if (!sectionData) {
    std::cerr << "Section '" << opts.sectionName << "' not found.\n";
    listSections(obj);
    return 1;
  }

  std::cout << std::format("Section '{}': address=0x{:x}  size={} bytes\n\n",
                           opts.sectionName, sectionAddress, sectionSize);

  // Disassemble
  dragon::Configuration cfg;
  cfg.disassemblyStrategy = opts.strategy;
  cfg.syntax = opts.syntax;
  cfg.logging = true;

  dragon::Disassembler dis(arch, cfg);
  if (!dis.isValid()) {
    std::cerr << "Failed to create disassembler for "
              << dragon::ArchitectureName(arch) << '\n';
    return 1;
  }

  dragon::Graph g(arch, cfg);
  dis.disassemble(std::span{sectionData, sectionSize}, sectionAddress, g);

  if (g.empty()) {
    std::cerr << "No instructions decoded — check architecture or section.\n";
    return 1;
  }

  // Print basic blocks
  const std::string sep(72, '-');
  const std::string hdr(72, '=');
  std::cout << hdr << '\n'
            << std::format(
                       "  Disassembly of '{}' ({} instructions, {} blocks)\n",
                       opts.sectionName, g.size(), g.blocks().size())
            << hdr << '\n';

  for (const dragon::BasicBlock &block: g.blocks()) {
    std::cout << std::format("\nBlock @ 0x{:x}\n{}\n", block.startAddress(),
                             sep);
    auto ignilBlock = g.getIGNILBlock(block.startAddress());
    for (const dragon::Instruction *insn: block.instructions()) {
      std::cout << std::format("  {:016x}  {}\n", insn->address(),
                               insn->disassembly());
      if (ignilBlock) {
        for (const dragon::ignil::Op &op: *ignilBlock) {
          if (op.address == insn->address())
            std::cout << "                      -> "
                      << dragon::ignil::toString(op) << '\n';
        }
      }
    }
  }
  std::cout << '\n';

  return 0;
}
