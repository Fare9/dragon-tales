#include <catch2/catch_test_macros.hpp>

#include <dragon/sigs/PatternParser.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("PatternParser - parses a simple line with one name",
         "[sigs][pattern-parser]") {
  auto sig = dragon::sigs::PatternParser::parseLine(
          "558bec83ec105356578b7d08 04 1234 0020 :0000 _main");
  REQUIRE(sig.has_value());
  REQUIRE(sig->leadingBytes.size() == 12);
  REQUIRE(sig->crcLength == 0x04);
  REQUIRE(sig->crc16 == 0x1234);
  REQUIRE(sig->totalLength == 0x20);
  REQUIRE(sig->publicNames.size() == 1);
  REQUIRE(sig->publicNames[0].offset == 0);
  REQUIRE(sig->publicNames[0].name == "_main");
}

TEST_CASE("PatternParser - leading bytes decode correctly", "[sigs][pattern-parser]") {
  auto sig = dragon::sigs::PatternParser::parseLine("558bec 00 0000 0003 :0000 f");
  REQUIRE(sig.has_value());
  REQUIRE(sig->leadingBytes.size() == 3);
  REQUIRE_FALSE(sig->leadingBytes[0].wildcard);
  REQUIRE(sig->leadingBytes[0].value == 0x55);
  REQUIRE(sig->leadingBytes[1].value == 0x8b);
  REQUIRE(sig->leadingBytes[2].value == 0xec);
}

TEST_CASE("PatternParser - wildcard bytes are recognized", "[sigs][pattern-parser]") {
  auto sig =
          dragon::sigs::PatternParser::parseLine("55..ec 00 0000 0003 :0000 f");
  REQUIRE(sig.has_value());
  REQUIRE(sig->leadingBytes.size() == 3);
  REQUIRE_FALSE(sig->leadingBytes[0].wildcard);
  REQUIRE(sig->leadingBytes[1].wildcard);
  REQUIRE_FALSE(sig->leadingBytes[2].wildcard);
}

TEST_CASE("PatternParser - multiple :offset name pairs", "[sigs][pattern-parser]") {
  auto sig = dragon::sigs::PatternParser::parseLine(
          "5589e5 00 0000 0010 :0000 outer :0008 inner");
  REQUIRE(sig.has_value());
  REQUIRE(sig->publicNames.size() == 2);
  REQUIRE(sig->publicNames[0].offset == 0x0000);
  REQUIRE(sig->publicNames[0].name == "outer");
  REQUIRE(sig->publicNames[1].offset == 0x0008);
  REQUIRE(sig->publicNames[1].name == "inner");
}

TEST_CASE("PatternParser - referenced-name extension truncates gracefully "
         "instead of failing the line",
         "[sigs][pattern-parser]") {
  // ^offset refname is a real-name-crossreference extension this parser
  // doesn't implement (see class doc) - the primary signature should still
  // parse.
  auto sig = dragon::sigs::PatternParser::parseLine(
          "5589e5 00 0000 0010 :0000 outer ^0004 something_else");
  REQUIRE(sig.has_value());
  REQUIRE(sig->publicNames.size() == 1);
  REQUIRE(sig->publicNames[0].name == "outer");
}

TEST_CASE("PatternParser - rejects malformed lines", "[sigs][pattern-parser]") {
  SECTION("too few fields") {
    REQUIRE_FALSE(dragon::sigs::PatternParser::parseLine("5589e5 00 0000").has_value());
  }
  SECTION("odd-length hex pattern") {
    REQUIRE_FALSE(
            dragon::sigs::PatternParser::parseLine("558 00 0000 0010 :0000 f")
                    .has_value());
  }
  SECTION("non-hex byte in pattern") {
    REQUIRE_FALSE(
            dragon::sigs::PatternParser::parseLine("zz89e5 00 0000 0010 :0000 f")
                    .has_value());
  }
  SECTION("no public names") {
    REQUIRE_FALSE(
            dragon::sigs::PatternParser::parseLine("5589e5 00 0000 0010").has_value());
  }
}

TEST_CASE("PatternParser - skips comments and blank lines", "[sigs][pattern-parser]") {
  REQUIRE_FALSE(dragon::sigs::PatternParser::parseLine("").has_value());
  REQUIRE_FALSE(dragon::sigs::PatternParser::parseLine("   ").has_value());
  REQUIRE_FALSE(
          dragon::sigs::PatternParser::parseLine("; a comment line").has_value());
  REQUIRE_FALSE(dragon::sigs::PatternParser::parseLine("# also a comment").has_value());
  REQUIRE_FALSE(dragon::sigs::PatternParser::parseLine("---").has_value());
}

TEST_CASE("PatternParser - parses a whole file, skipping bad lines",
         "[sigs][pattern-parser]") {
  const std::filesystem::path path =
          std::filesystem::temp_directory_path() / "dragon_test_sigs.pat";
  {
    std::ofstream out(path);
    out << "; a comment\n";
    out << "558bec 00 0000 0003 :0000 first\n";
    out << "\n";
    out << "this line is garbage\n";
    out << "5589e5 00 0000 0003 :0000 second\n";
  }

  auto sigs = dragon::sigs::PatternParser::parseFile(path);
  std::filesystem::remove(path);

  REQUIRE(sigs.size() == 2);
  REQUIRE(sigs[0].publicNames[0].name == "first");
  REQUIRE(sigs[1].publicNames[0].name == "second");
}

TEST_CASE("PatternParser - parseFile returns empty for a missing file",
         "[sigs][pattern-parser]") {
  auto sigs = dragon::sigs::PatternParser::parseFile("/nonexistent/path/x.pat");
  REQUIRE(sigs.empty());
}
