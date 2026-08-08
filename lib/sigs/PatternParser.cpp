#include <dragon/sigs/PatternParser.hpp>

#include <cctype>
#include <charconv>
#include <fstream>

namespace {

using dragon::sigs::FunctionRef;
using dragon::sigs::PatternByte;
using dragon::sigs::Signature;

bool parseHexByte(std::string_view hex, std::uint8_t &out) {
  if (hex.size() != 2)
    return false;
  unsigned value = 0;
  auto [ptr, ec] =
          std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
  if (ec != std::errc{} || ptr != hex.data() + hex.size())
    return false;
  out = static_cast<std::uint8_t>(value);
  return true;
}

template <typename T>
bool parseHex(std::string_view hex, T &out) {
  if (hex.empty())
    return false;
  T value{};
  auto [ptr, ec] =
          std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
  if (ec != std::errc{} || ptr != hex.data() + hex.size())
    return false;
  out = value;
  return true;
}

std::optional<std::vector<PatternByte>> parseHexPattern(std::string_view pat) {
  if (pat.size() % 2 != 0)
    return std::nullopt;
  std::vector<PatternByte> result;
  result.reserve(pat.size() / 2);
  for (std::size_t i = 0; i < pat.size(); i += 2) {
    std::string_view pair = pat.substr(i, 2);
    PatternByte pb;
    if (pair == "..") {
      pb.wildcard = true;
    } else {
      std::uint8_t value = 0;
      if (!parseHexByte(pair, value))
        return std::nullopt;
      pb.value = value;
    }
    result.push_back(pb);
  }
  return result;
}

std::vector<std::string_view> splitWhitespace(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])))
      ++i;
    const std::size_t start = i;
    while (i < line.size() &&
           !std::isspace(static_cast<unsigned char>(line[i])))
      ++i;
    if (i > start)
      tokens.push_back(line.substr(start, i - start));
  }
  return tokens;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  return s;
}

} // namespace

std::optional<Signature>
dragon::sigs::PatternParser::parseLine(std::string_view line) {
  line = trim(line);
  if (line.empty() || line.front() == ';' || line.front() == '#' ||
      line == "---")
    return std::nullopt;

  const auto tokens = splitWhitespace(line);
  if (tokens.size() < 4)
    return std::nullopt;

  Signature sig;

  auto leading = parseHexPattern(tokens[0]);
  if (!leading)
    return std::nullopt;
  sig.leadingBytes = std::move(*leading);

  unsigned crcLen = 0;
  if (!parseHex(tokens[1], crcLen) || crcLen > 0xFFu)
    return std::nullopt;
  sig.crcLength = static_cast<std::uint8_t>(crcLen);

  unsigned crc = 0;
  if (!parseHex(tokens[2], crc) || crc > 0xFFFFu)
    return std::nullopt;
  sig.crc16 = static_cast<std::uint16_t>(crc);

  unsigned totalLen = 0;
  if (!parseHex(tokens[3], totalLen))
    return std::nullopt;
  sig.totalLength = totalLen;

  // :offset name pairs. A `^` (referenced-name) or any other unrecognized
  // token ends the name list - see the class doc for why that's an
  // intentional scope cut rather than a parse failure.
  for (std::size_t i = 4; i < tokens.size(); ++i) {
    if (tokens[i].empty() || tokens[i].front() != ':')
      break;
    unsigned offset = 0;
    if (!parseHex(tokens[i].substr(1), offset))
      break;
    if (i + 1 >= tokens.size())
      break;
    FunctionRef ref;
    ref.offset = offset;
    ref.name = std::string(tokens[i + 1]);
    sig.publicNames.push_back(std::move(ref));
    ++i;
  }

  if (sig.publicNames.empty())
    return std::nullopt;

  return sig;
}

std::vector<Signature>
dragon::sigs::PatternParser::parseFile(const std::filesystem::path &path) {
  std::vector<Signature> result;
  std::ifstream file(path);
  if (!file)
    return result;

  std::string line;
  while (std::getline(file, line)) {
    if (auto sig = parseLine(line))
      result.push_back(std::move(*sig));
  }
  return result;
}
