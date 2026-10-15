#include <dragon/loader/Loader.hpp>

#include <algorithm>

#include <llvm/BinaryFormat/COFF.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/Object/COFF.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/TargetParser/Triple.h>

namespace dragon::loader {

using namespace llvm;
using namespace llvm::object;

std::string_view formatName(Format f) {
  switch (f) {
    case Format::ELF: return "elf";
    case Format::PE: return "pe";
    default: return "unknown";
  }
}

namespace {

/// Record an LLVM error into the optional out-parameter and discard it.
void report(std::string *error, Error e) {
  if (error)
    *error = toString(std::move(e));
  else
    consumeError(std::move(e));
}

void report(std::string *error, const std::string &message) {
  if (error)
    *error = message;
}

/// Map an LLVM triple to the dragon architecture enum. Unknown for anything
/// dragon does not model.
Architecture archOf(Triple::ArchType a) {
  switch (a) {
    case Triple::x86: return Architecture::X86_32;
    case Triple::x86_64: return Architecture::X86_64;
    case Triple::arm:
    case Triple::armeb:
    case Triple::thumb:
    case Triple::thumbeb: return Architecture::ARM32;
    case Triple::aarch64:
    case Triple::aarch64_be: return Architecture::ARM64;
    case Triple::riscv32: return Architecture::RISCV32;
    case Triple::riscv64: return Architecture::RISCV64;
    default: return Architecture::Unknown;
  }
}

SymbolKind kindOf(SymbolRef::Type t) {
  switch (t) {
    case SymbolRef::ST_Function: return SymbolKind::Function;
    case SymbolRef::ST_Data: return SymbolKind::Object;
    case SymbolRef::ST_File: return SymbolKind::File;
    case SymbolRef::ST_Other:
    default: return SymbolKind::Unknown;
  }
}

/// Copy the PT_LOAD program headers out as Sections named "LOAD<n>". File
/// bytes are clamped to what the buffer really holds, so a truncated or
/// hostile header cannot read past the end.
template <class ELFT>
void collectSegments(const ELFFile<ELFT> &file, std::vector<Section> &out) {
  auto phdrs = file.program_headers();
  if (!phdrs) {
    consumeError(phdrs.takeError());
    return;
  }
  const std::uint64_t fileSize = file.getBufSize();
  for (const auto &ph : *phdrs) {
    if (ph.p_type != ELF::PT_LOAD || ph.p_memsz == 0)
      continue;
    Section s;
    s.name = "LOAD" + std::to_string(out.size());
    s.address = ph.p_vaddr;
    s.virtualSize = ph.p_memsz;
    s.readable = ph.p_flags & ELF::PF_R;
    s.writable = ph.p_flags & ELF::PF_W;
    s.executable = ph.p_flags & ELF::PF_X;
    if (ph.p_offset < fileSize) {
      const std::uint64_t n = std::min<std::uint64_t>(
          {ph.p_filesz, ph.p_memsz, fileSize - ph.p_offset});
      const std::uint8_t *base = file.base() + ph.p_offset;
      s.bytes.assign(base, base + n);
    }
    out.push_back(std::move(s));
  }
}

void collectSegments(const ObjectFile *obj, std::vector<Section> &out) {
  if (const auto *e = dyn_cast<ELF64LEObjectFile>(obj))
    collectSegments(e->getELFFile(), out);
  else if (const auto *e = dyn_cast<ELF64BEObjectFile>(obj))
    collectSegments(e->getELFFile(), out);
  else if (const auto *e = dyn_cast<ELF32LEObjectFile>(obj))
    collectSegments(e->getELFFile(), out);
  else if (const auto *e = dyn_cast<ELF32BEObjectFile>(obj))
    collectSegments(e->getELFFile(), out);
}

/// Read `size` bytes at `addr` from one section/segment, zero-filling past its
/// file bytes. nullopt if the run does not fit inside it.
std::optional<std::vector<std::uint8_t>>
readFrom(const Section &s, std::uint64_t addr, std::size_t size) {
  if (!s.contains(addr) || addr + size > s.address + s.virtualSize)
    return std::nullopt;
  std::vector<std::uint8_t> out(size, 0);
  const std::uint64_t off = addr - s.address;
  // Copy whatever file bytes overlap the request; the rest stays zero (BSS).
  if (off < s.bytes.size()) {
    const std::size_t n = std::min<std::size_t>(size, s.bytes.size() - off);
    std::copy_n(s.bytes.begin() + off, n, out.begin());
  }
  return out;
}

} // namespace

std::optional<LoadedImage> LoadedImage::loadFile(const std::string &path,
                                                 std::string *error) {
  auto buf = MemoryBuffer::getFile(path, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (!buf) {
    report(error, "cannot read '" + path + "': " + buf.getError().message());
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes((*buf)->getBufferStart(),
                                  (*buf)->getBufferEnd());
  return loadBytes(std::move(bytes), error);
}

std::optional<LoadedImage>
LoadedImage::loadBytes(std::vector<std::uint8_t> bytes, std::string *error) {
  // The MemoryBuffer only references the bytes; keep them alive for the whole
  // extraction by holding them in a local that outlives the ObjectFile.
  StringRef data(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  auto bufRef = MemoryBufferRef(data, "image");

  auto objOrErr = ObjectFile::createObjectFile(bufRef);
  if (!objOrErr) {
    report(error, objOrErr.takeError());
    return std::nullopt;
  }
  ObjectFile *obj = objOrErr->get();

  LoadedImage image;
  image.arch_ = archOf(obj->getArch());
  image.is64_ = obj->getBytesInAddress() == 8;

  const auto *elf = dyn_cast<ELFObjectFileBase>(obj);
  const auto *coff = dyn_cast<COFFObjectFile>(obj);
  if (elf)
    image.format_ = Format::ELF;
  else if (coff)
    image.format_ = Format::PE;
  else {
    report(error, std::string("unsupported object format (only ELF and "
                              "PE/COFF are handled)"));
    return std::nullopt;
  }

  // -- image base ---------------------------------------------------------
  if (coff)
    image.imageBase_ = coff->getImageBase();
  // ELF image base is filled in below from the PT_LOAD segments.

  // -- sections -----------------------------------------------------------
  std::uint64_t lowestAddr = UINT64_MAX;
  for (const SectionRef &sec : obj->sections()) {
    Section s;
    if (auto name = sec.getName())
      s.name = name->str();

    if (coff) {
      const coff_section *cs = coff->getCOFFSection(sec);
      s.address = image.imageBase_ + cs->VirtualAddress;
      s.virtualSize = cs->VirtualSize ? cs->VirtualSize : cs->SizeOfRawData;
      const auto c = cs->Characteristics;
      s.executable = c & COFF::IMAGE_SCN_MEM_EXECUTE;
      s.readable = c & COFF::IMAGE_SCN_MEM_READ;
      s.writable = c & COFF::IMAGE_SCN_MEM_WRITE;
    } else {
      s.address = sec.getAddress();
      s.virtualSize = sec.getSize();
      const std::uint64_t flags = ELFSectionRef(sec).getFlags();
      s.readable = flags & ELF::SHF_ALLOC;
      s.writable = flags & ELF::SHF_WRITE;
      s.executable = flags & ELF::SHF_EXECINSTR;
    }

    // BSS carries address space but no file bytes; everything else copies its
    // on-disk contents.
    if (!sec.isBSS() && !sec.isVirtual()) {
      if (auto contents = sec.getContents())
        s.bytes.assign(contents->bytes_begin(), contents->bytes_end());
      else
        consumeError(contents.takeError());
    }

    if (s.address != 0 && (s.readable || s.executable))
      lowestAddr = std::min(lowestAddr, s.address);

    image.sections_.push_back(std::move(s));
  }

  // -- segments -----------------------------------------------------------
  // The program headers are what the system loader maps. They are the only
  // layout left when a protector strips the section headers, and the lowest
  // PT_LOAD address is the real ELF image base (0 for a PIE).
  if (elf)
    collectSegments(obj, image.segments_);

  image.hasVirtualLayout_ =
      !image.segments_.empty() ||
      std::any_of(image.sections_.begin(), image.sections_.end(),
                  [](const Section &s) { return s.address != 0; });

  if (!image.segments_.empty())
    image.imageBase_ =
        std::min_element(image.segments_.begin(), image.segments_.end(),
                         [](const Section &a, const Section &b) {
                           return a.address < b.address;
                         })->address;
  else if (elf && lowestAddr != UINT64_MAX)
    image.imageBase_ = lowestAddr;

  // -- entry point --------------------------------------------------------
  if (coff) {
    std::uint32_t rva = 0;
    if (const auto *pe = coff->getPE32PlusHeader())
      rva = pe->AddressOfEntryPoint;
    else if (const auto *pe = coff->getPE32Header())
      rva = pe->AddressOfEntryPoint;
    if (rva != 0)
      image.entry_ = image.imageBase_ + rva;
  } else if (auto start = obj->getStartAddress()) {
    // ELF e_entry; zero for a relocatable object, which has no entry.
    if (*start != 0)
      image.entry_ = *start;
  } else {
    consumeError(start.takeError());
  }

  // -- symbols ------------------------------------------------------------
  for (const SymbolRef &sym : obj->symbols()) {
    Symbol out;
    if (auto name = sym.getName())
      out.name = name->str();
    else
      consumeError(name.takeError());

    if (out.name.empty())
      continue; // skip the anonymous section/aux entries COFF emits

    if (auto type = sym.getType())
      out.kind = kindOf(*type);
    else
      consumeError(type.takeError());

    const std::uint32_t flags = sym.getFlags().get();
    out.imported = flags & SymbolRef::SF_Undefined;
    out.exported = (flags & SymbolRef::SF_Exported) ||
                   ((flags & SymbolRef::SF_Global) && !out.imported);

    if (!out.imported) {
      if (auto addr = sym.getAddress())
        out.address = *addr;
      else
        consumeError(addr.takeError());
    }
    // ELF symbols carry a size; COFF symbols do not, so it stays zero there.
    if (elf)
      out.size = ELFSymbolRef(sym).getSize();

    image.symbols_.push_back(std::move(out));
  }

  return image;
}

const Section *LoadedImage::sectionContaining(std::uint64_t addr) const {
  for (const Section &s : sections_)
    if (s.contains(addr))
      return &s;
  return nullptr;
}

std::vector<const Section *> LoadedImage::executableSections() const {
  std::vector<const Section *> out;
  for (const Section &s : sections_)
    if (s.executable && !s.bytes.empty())
      out.push_back(&s);
  if (out.empty())
    for (const Section &s : segments_)
      if (s.executable && !s.bytes.empty())
        out.push_back(&s);
  std::sort(out.begin(), out.end(), [](const Section *a, const Section *b) {
    return a->address < b->address;
  });
  return out;
}

std::optional<std::vector<std::uint8_t>>
LoadedImage::readVirtual(std::uint64_t addr, std::size_t size) const {
  if (const Section *s = sectionContaining(addr))
    if (auto out = readFrom(*s, addr, size))
      return out;
  // No section covers the run (stripped section headers, or a read spanning
  // two sections): fall back to the segment the loader would map.
  for (const Section &s : segments_)
    if (auto out = readFrom(s, addr, size))
      return out;
  return std::nullopt;
}

std::vector<const Symbol *> LoadedImage::definedSymbols() const {
  std::vector<const Symbol *> out;
  for (const Symbol &s : symbols_)
    if (!s.imported)
      out.push_back(&s);
  std::sort(out.begin(), out.end(), [](const Symbol *a, const Symbol *b) {
    return a->address < b->address;
  });
  return out;
}

std::vector<const Symbol *> LoadedImage::imports() const {
  std::vector<const Symbol *> out;
  for (const Symbol &s : symbols_)
    if (s.imported)
      out.push_back(&s);
  return out;
}

std::vector<const Symbol *> LoadedImage::exports() const {
  std::vector<const Symbol *> out;
  for (const Symbol &s : symbols_)
    if (s.exported)
      out.push_back(&s);
  return out;
}

const Symbol *LoadedImage::functionAt(std::uint64_t addr) const {
  for (const Symbol &s : symbols_)
    if (!s.imported && s.kind == SymbolKind::Function && s.address == addr)
      return &s;
  return nullptr;
}

} // namespace dragon::loader
