#include <catch2/catch_test_macros.hpp>

#include <dragon/sigs/PatternParser.hpp>
#include <dragon/sigs/SignatureDB.hpp>
#include <dragon/sigs/SignatureMatcher.hpp>

#include <format>

using namespace dragon::sigs;

TEST_CASE("SignatureMatcher - computeCRC16 of empty data is the init value",
         "[sigs][signature-matcher]") {
  REQUIRE(SignatureMatcher::computeCRC16({}) == 0xFFFF);
}

TEST_CASE("SignatureMatcher - computeCRC16 is deterministic and "
         "input-sensitive",
         "[sigs][signature-matcher]") {
  const std::vector<std::uint8_t> a = {0x01, 0x02, 0x03, 0x04};
  const std::vector<std::uint8_t> b = {0x01, 0x02, 0x03, 0x05};
  REQUIRE(SignatureMatcher::computeCRC16(a) == SignatureMatcher::computeCRC16(a));
  REQUIRE(SignatureMatcher::computeCRC16(a) != SignatureMatcher::computeCRC16(b));
}

static Signature makeSignature(std::vector<PatternByte> leading,
                               std::uint8_t crcLen, std::uint16_t crc,
                               std::uint32_t totalLen,
                               std::vector<FunctionRef> names) {
  Signature sig;
  sig.leadingBytes = std::move(leading);
  sig.crcLength = crcLen;
  sig.crc16 = crc;
  sig.totalLength = totalLen;
  sig.publicNames = std::move(names);
  return sig;
}

TEST_CASE("SignatureMatcher - matchAt succeeds when leading bytes and CRC "
         "both check out",
         "[sigs][signature-matcher]") {
  // Candidate bytes: 55 8b ec [10 20 30 40] c3  (8 bytes total)
  // Leading pattern: 55 8b ec (fixed, no wildcard)
  // CRC region: the 4 bytes right after (10 20 30 40)
  const std::vector<std::uint8_t> candidate = {0x55, 0x8b, 0xec, 0x10,
                                               0x20, 0x30, 0x40, 0xc3};
  const auto crc =
          SignatureMatcher::computeCRC16(std::span(candidate).subspan(3, 4));

  SignatureDB db;
  db.addSignatures({makeSignature(
          {{0x55, false}, {0x8b, false}, {0xec, false}}, 4, crc, 8,
          {{0, "func"}})});

  auto match = SignatureMatcher::matchAt(db, candidate, 0x4000);
  REQUIRE(match.has_value());
  REQUIRE(match->address == 0x4000);
  REQUIRE(match->name == "func");
  REQUIRE(match->length == 8);
}

TEST_CASE("SignatureMatcher - wildcard bytes match anything",
         "[sigs][signature-matcher]") {
  const std::vector<std::uint8_t> candidateA = {0x55, 0x01, 0xec};
  const std::vector<std::uint8_t> candidateB = {0x55, 0xff, 0xec};

  SignatureDB db;
  db.addSignatures({makeSignature(
          {{0x55, false}, {0, true}, {0xec, false}}, 0, 0, 0, {{0, "f"}})});

  REQUIRE(SignatureMatcher::matchAt(db, candidateA, 0x1000).has_value());
  REQUIRE(SignatureMatcher::matchAt(db, candidateB, 0x1000).has_value());
}

TEST_CASE("SignatureMatcher - a CRC mismatch fails the match",
         "[sigs][signature-matcher]") {
  const std::vector<std::uint8_t> candidate = {0x55, 0x8b, 0xec, 0x10, 0x20};

  SignatureDB db;
  db.addSignatures({makeSignature({{0x55, false}, {0x8b, false}, {0xec, false}},
                                  2, 0xDEAD /* deliberately wrong */, 5,
                                  {{0, "func"}})});

  REQUIRE_FALSE(SignatureMatcher::matchAt(db, candidate, 0x1000).has_value());
}

TEST_CASE("SignatureMatcher - totalLength gates insufficient candidate bytes",
         "[sigs][signature-matcher]") {
  const std::vector<std::uint8_t> shortCandidate = {0x55, 0x8b};

  SignatureDB db;
  db.addSignatures(
          {makeSignature({{0x55, false}, {0x8b, false}}, 0, 0, 16, {{0, "f"}})});

  REQUIRE_FALSE(SignatureMatcher::matchAt(db, shortCandidate, 0x1000).has_value());
}

TEST_CASE("SignatureMatcher - matchEntries reports every public name in a "
         "matching module",
         "[sigs][signature-matcher]") {
  const std::vector<std::uint8_t> image = {0x55, 0x8b, 0xec, 0x90, 0x90};

  SignatureDB db;
  db.addSignatures({makeSignature({{0x55, false}, {0x8b, false}, {0xec, false}},
                                  0, 0, 0, {{0x0, "outer"}, {0x3, "inner"}})});

  auto matches = SignatureMatcher::matchEntries(db, image, 0x1000, {0x1000});
  REQUIRE(matches.size() == 2);
  REQUIRE(matches[0].address == 0x1000);
  REQUIRE(matches[0].name == "outer");
  REQUIRE(matches[1].address == 0x1003);
  REQUIRE(matches[1].name == "inner");
}

TEST_CASE("SignatureMatcher - matchEntries only tries addresses within the "
         "image",
         "[sigs][signature-matcher]") {
  const std::vector<std::uint8_t> image = {0x55, 0x8b, 0xec};

  SignatureDB db;
  db.addSignatures({makeSignature({{0x55, false}}, 0, 0, 0, {{0, "f"}})});

  // 0x0FFF is below imageBase, 0x2000 is past the end of the image.
  auto matches = SignatureMatcher::matchEntries(db, image, 0x1000,
                                                {0x0FFF, 0x1000, 0x2000});
  REQUIRE(matches.size() == 1);
  REQUIRE(matches[0].address == 0x1000);
}

// -----------------------------------------------------------------------
// Full pipeline: a .pat text line, parsed, with a CRC this test computes
// itself via the same computeCRC16 the matcher uses - proves parser and
// matcher agree end to end without depending on an external reference file.
// -----------------------------------------------------------------------

TEST_CASE("SignatureMatcher - full pipeline: parse a .pat line then match it",
         "[sigs][signature-matcher][integration]") {
  const std::vector<std::uint8_t> candidate = {0x55, 0x89, 0xe5, 0xaa,
                                               0xbb, 0xcc, 0xc3};
  // Leading pattern: 55 89 e5 (push rbp; mov ebp,esp - as bytes, not
  // actually decoded here, just used as pattern bytes).
  // CRC region: the 3 bytes right after (aa bb cc).
  const auto crc =
          SignatureMatcher::computeCRC16(std::span(candidate).subspan(3, 3));

  const std::string line =
          std::format("5589e5 03 {:04x} {:04x} :0000 known_func", crc,
                     candidate.size());

  auto parsed = dragon::sigs::PatternParser::parseLine(line);
  REQUIRE(parsed.has_value());

  SignatureDB db;
  db.addSignatures({std::move(*parsed)});

  auto match = SignatureMatcher::matchAt(db, candidate, 0x7000);
  REQUIRE(match.has_value());
  REQUIRE(match->name == "known_func");
  REQUIRE(match->address == 0x7000);
}
