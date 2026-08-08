#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dragon::sigs {

/// One byte of a FLIRT pattern: either a fixed value the candidate bytes
/// must match exactly, or a wildcard (matches anything - used for bytes
/// that vary between builds, e.g. relocation operands).
struct PatternByte {
  std::uint8_t value = 0;
  bool wildcard = false;
};

/// A name associated with a specific byte offset inside a matched
/// signature. Every Signature has at least one (the primary function name
/// at offset 0); some FLIRT modules bundle several adjacent functions
/// under one pattern and give each its own offset/name pair.
struct FunctionRef {
  std::uint32_t offset = 0;
  std::string name;
};

/// One parsed signature: a function's leading-byte pattern (with
/// wildcards), a CRC16-verified region right after it, and the name(s) it
/// identifies. Corresponds to one line of a FLIRT .pat file.
struct Signature {
  std::vector<PatternByte> leadingBytes; // conventionally 32 bytes
  std::uint16_t crc16 = 0;
  std::uint8_t crcLength = 0;    // bytes right after leadingBytes covered by crc16
  std::uint32_t totalLength = 0; // total function length in bytes, 0 = unknown
  std::vector<FunctionRef> publicNames;
};

/// A successful match of a Signature against a specific address.
struct SignatureMatch {
  std::uint64_t address = 0;
  std::string name;
  std::uint32_t length = 0;
};

} // namespace dragon::sigs
