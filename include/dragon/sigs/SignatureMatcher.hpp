#pragma once

#include <dragon/sigs/Signature.hpp>
#include <dragon/sigs/SignatureDB.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dragon::sigs {

/// Matches FLIRT signatures against raw bytes.
///
/// Matching, in order:
///   1. Leading-byte pattern (wildcards always match).
///   2. CRC16 of `crcLength` bytes immediately after the leading pattern.
///   3. totalLength, if nonzero, gates that enough bytes are available.
///
/// Ambiguity note: real FLIRT disambiguates near-identical functions
/// further by following "referenced name" cross-references (does this
/// function call the same targets the signature module expects?) and
/// alternate-branch patterns for functions that diverge partway through.
/// Neither is implemented here - PatternParser only extracts the primary
/// leading-pattern + CRC + name list (see its class doc). Two signatures
/// with identical leading bytes, CRC region, and totalLength are
/// indistinguishable to this matcher; matchAt/matchEntries return
/// whichever the SignatureDB has first. In practice this is rare (the
/// CRC-verified region is highly discriminating), but it's a real, known
/// simplification, not silently-assumed correctness.
class SignatureMatcher {
  public:
  /// Computes the CRC16 dragon-tales' signature format uses: reflected,
  /// polynomial 0xA001 (CRC-16/ARC), init 0xFFFF, no final XOR.
  ///
  /// Caveat: this could not be verified against a real IDA/FLAIR-generated
  /// .pat file while implementing it (no such file or network access to
  /// check against was available) - it's a well-known, standard CRC16
  /// variant and self-consistent with this codebase's own
  /// PatternParser/SignatureDB, but if you load a real-world .pat database
  /// and nothing ever verifies, this constant is the first thing to check.
  [[nodiscard]] static std::uint16_t
  computeCRC16(std::span<const std::uint8_t> data);

  /// Tries every signature in `db` against `bytes`, which must begin at
  /// the candidate function's address. Returns the first match's primary
  /// name (publicNames.front(), conventionally the offset-0 entry), if
  /// any signature matches.
  [[nodiscard]] static std::optional<SignatureMatch>
  matchAt(const SignatureDB &db, std::span<const std::uint8_t> bytes,
         std::uint64_t address);

  /// Tries every address in `entryPoints` against every signature in `db`.
  /// `image`/`imageBase` describe the byte region entryPoints fall within.
  /// Unlike matchAt, every name a matching signature defines is reported
  /// (a module can bundle more than one function/offset under one
  /// pattern), so one entry point may produce more than one match.
  [[nodiscard]] static std::vector<SignatureMatch>
  matchEntries(const SignatureDB &db, std::span<const std::uint8_t> image,
              std::uint64_t imageBase,
              const std::vector<std::uint64_t> &entryPoints);

  private:
  [[nodiscard]] static bool matches(const Signature &sig,
                                    std::span<const std::uint8_t> bytes);
};

} // namespace dragon::sigs
