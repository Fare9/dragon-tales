#include <dragon/loader/ImageDisassembler.hpp>

#include <dragon/disassembler/Disassembler.hpp>

namespace dragon::loader {

Graph disassembleImage(const LoadedImage &image, const Configuration &config) {
  Graph graph(image.architecture(), config);
  if (image.architecture() == Architecture::Unknown)
    return graph;

  Disassembler dis(image.architecture(), config);
  for (const Section *s : image.executableSections())
    dis.disassemble(s->bytes, s->address, graph);

  // Seed function starts from the image's own metadata. addFunction only marks
  // an address as a valid function entry, so it is safe to call after the
  // sections have been disassembled.
  if (auto entry = image.entryPoint())
    graph.addFunction(*entry);
  for (const Symbol *sym : image.definedSymbols())
    if (sym->kind == SymbolKind::Function)
      graph.addFunction(sym->address);

  return graph;
}

} // namespace dragon::loader
