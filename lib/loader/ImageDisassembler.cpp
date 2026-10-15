#include <dragon/loader/ImageDisassembler.hpp>

#include <dragon/disassembler/Disassembler.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace dragon::loader {

Graph disassembleImage(const LoadedImage &image, const Configuration &config) {
  Graph graph(image.architecture(), config);
  if (image.architecture() == Architecture::Unknown)
    return graph;

  // Known code addresses: the entry point and every function symbol.
  std::vector<std::uint64_t> seeds;
  if (auto entry = image.entryPoint())
    seeds.push_back(*entry);
  for (const Symbol *sym : image.definedSymbols())
    if (sym->kind == SymbolKind::Function)
      seeds.push_back(sym->address);

  // With no executable section, executableSections() hands back segments. A
  // segment can map the ELF and program headers in front of the code, and a
  // linear sweep through them falls out of step before reaching it, so start
  // each such sweep at the first known code address it contains.
  const bool fromSegments =
      std::none_of(image.sections().begin(), image.sections().end(),
                   [](const Section &s) {
                     return s.executable && !s.bytes.empty();
                   });

  Disassembler dis(image.architecture(), config);
  for (const Section *s : image.executableSections()) {
    std::uint64_t start = s->address;
    if (fromSegments) {
      std::uint64_t first = UINT64_MAX;
      for (std::uint64_t a : seeds)
        if (a >= s->address && a < s->address + s->bytes.size())
          first = std::min(first, a);
      if (first != UINT64_MAX)
        start = first;
    }
    const std::span<const std::uint8_t> bytes(s->bytes);
    dis.disassemble(bytes.subspan(start - s->address), start, graph);
  }

  // Seed function starts from the image's own metadata. addFunction only marks
  // an address as a valid function entry, so it is safe to call after the
  // sections have been disassembled.
  for (std::uint64_t a : seeds)
    graph.addFunction(a);

  return graph;
}

} // namespace dragon::loader
