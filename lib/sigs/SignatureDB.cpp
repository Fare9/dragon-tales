#include <dragon/sigs/SignatureDB.hpp>
#include <dragon/sigs/PatternParser.hpp>

#include <system_error>

void dragon::sigs::SignatureDB::loadFile(const std::filesystem::path &path) {
  auto parsed = PatternParser::parseFile(path);
  addSignatures(std::move(parsed));
}

void dragon::sigs::SignatureDB::loadDirectory(
        const std::filesystem::path &dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec))
    return;

  for (const auto &entry: std::filesystem::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() == ".pat")
      loadFile(entry.path());
  }
}

void dragon::sigs::SignatureDB::addSignatures(
        std::vector<Signature> newSignatures) {
  signatures_.insert(signatures_.end(),
                     std::make_move_iterator(newSignatures.begin()),
                     std::make_move_iterator(newSignatures.end()));
}
