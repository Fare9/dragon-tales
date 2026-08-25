#pragma once

#include <dragon/sigs/Signature.hpp>

#include <filesystem>
#include <vector>

namespace dragon::sigs {

/// A loaded collection of FLIRT .pat signatures, ready for matching via
/// SignatureMatcher.
class SignatureDB {
  public:
  /// Loads and appends every `.pat` file directly inside `dir` (not
  /// recursive). Files that fail to parse are silently skipped; check
  /// signatureCount() before/after to see how much was actually loaded.
  void loadDirectory(const std::filesystem::path &dir);

  /// Loads and appends a single `.pat` file. No-op if it can't be parsed.
  void loadFile(const std::filesystem::path &path);

  /// Appends already-parsed signatures directly - e.g. when the caller
  /// parsed the file itself, or for programmatically-built signatures.
  void addSignatures(std::vector<Signature> newSignatures);

  [[nodiscard]] const std::vector<Signature> &signatures() const {
    return signatures_;
  }
  [[nodiscard]] std::size_t signatureCount() const { return signatures_.size(); }

  private:
  std::vector<Signature> signatures_;
};

} // namespace dragon::sigs
