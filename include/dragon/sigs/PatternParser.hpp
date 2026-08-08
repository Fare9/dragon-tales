#pragma once

#include <dragon/sigs/Signature.hpp>

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace dragon::sigs {

/// Parses IDA FLIRT-compatible .pat text signature files (the format
/// produced by IDA/FLAIR's `sigmake`/`pelf` tools and used by most
/// community-maintained signature databases).
///
/// One function per line:
///   <hex pattern, `..` = wildcard byte> <crc length, hex byte>
///   <crc16, hex> <total length, hex> :<offset, hex> <name>
///   [:<offset> <name> ...]
///
/// Example:
///   558bec83ec..5356578b7d08 0a 1234 0042 :0000 _main
///
/// Scope: text .pat format only. The proprietary binary .sig format
/// (tree-compressed, used by IDA's bundled signature packs) is not
/// supported. Lines using FLIRT's "referenced name" (`^offset name`) or
/// alternate-branch pattern extensions are parsed up through the public
/// name list; anything after that is ignored rather than rejecting the
/// whole line - the primary signature still matches, just without that
/// extra disambiguation (see SignatureMatcher's class doc).
class PatternParser {
  public:
  /// Parses a single .pat line. Returns std::nullopt for blank lines,
  /// comments (`;`/`#`), the `---` module separator IDA's tools emit, or
  /// lines that don't have the minimum required fields.
  [[nodiscard]] static std::optional<Signature> parseLine(std::string_view line);

  /// Parses every line of a .pat file, silently skipping malformed or
  /// comment lines. Returns an empty vector if the file can't be opened.
  [[nodiscard]] static std::vector<Signature>
  parseFile(const std::filesystem::path &path);
};

} // namespace dragon::sigs
