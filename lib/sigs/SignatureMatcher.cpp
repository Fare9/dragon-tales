#include <dragon/sigs/SignatureMatcher.hpp>

std::uint16_t dragon::sigs::SignatureMatcher::computeCRC16(
        std::span<const std::uint8_t> data) {
  std::uint16_t crc = 0xFFFF;
  for (std::uint8_t byte: data) {
    crc = static_cast<std::uint16_t>(crc ^ byte);
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001)
                       : static_cast<std::uint16_t>(crc >> 1);
  }
  return crc;
}

bool dragon::sigs::SignatureMatcher::matches(
        const Signature &sig, std::span<const std::uint8_t> bytes) {
  if (sig.totalLength > 0 && bytes.size() < sig.totalLength)
    return false;

  if (bytes.size() < sig.leadingBytes.size())
    return false;
  for (std::size_t i = 0; i < sig.leadingBytes.size(); ++i) {
    if (sig.leadingBytes[i].wildcard)
      continue;
    if (bytes[i] != sig.leadingBytes[i].value)
      return false;
  }

  if (sig.crcLength > 0) {
    const std::size_t crcStart = sig.leadingBytes.size();
    const std::size_t crcEnd = crcStart + sig.crcLength;
    if (crcEnd > bytes.size())
      return false;
    if (computeCRC16(bytes.subspan(crcStart, sig.crcLength)) != sig.crc16)
      return false;
  }

  return true;
}

std::optional<dragon::sigs::SignatureMatch>
dragon::sigs::SignatureMatcher::matchAt(const SignatureDB &db,
                                        std::span<const std::uint8_t> bytes,
                                        std::uint64_t address) {
  for (const Signature &sig: db.signatures()) {
    if (sig.publicNames.empty() || !matches(sig, bytes))
      continue;
    const FunctionRef &primary = sig.publicNames.front();
    SignatureMatch match;
    match.address = address + primary.offset;
    match.name = primary.name;
    match.length = sig.totalLength;
    return match;
  }
  return std::nullopt;
}

std::vector<dragon::sigs::SignatureMatch>
dragon::sigs::SignatureMatcher::matchEntries(
        const SignatureDB &db, std::span<const std::uint8_t> image,
        std::uint64_t imageBase,
        const std::vector<std::uint64_t> &entryPoints) {
  std::vector<SignatureMatch> results;

  for (std::uint64_t entry: entryPoints) {
    if (entry < imageBase)
      continue;
    const std::uint64_t offset = entry - imageBase;
    if (offset >= image.size())
      continue;

    const auto candidate = image.subspan(offset);
    for (const Signature &sig: db.signatures()) {
      if (!matches(sig, candidate))
        continue;
      for (const FunctionRef &ref: sig.publicNames) {
        SignatureMatch match;
        match.address = entry + ref.offset;
        match.name = ref.name;
        match.length = sig.totalLength;
        results.push_back(std::move(match));
      }
    }
  }

  return results;
}
