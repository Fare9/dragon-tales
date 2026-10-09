#include <catch2/catch_test_macros.hpp>

#include <dragon/loader/ImageDisassembler.hpp>
#include <dragon/loader/Loader.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "fixtures/fixtures.hpp"

using namespace dragon;
using namespace dragon::loader;

namespace {

std::vector<std::uint8_t> toVec(const unsigned char *p, std::size_t n) {
  return std::vector<std::uint8_t>(p, p + n);
}

const Section *findSection(const LoadedImage &img, const std::string &name) {
  for (const Section &s : img.sections())
    if (s.name == name)
      return &s;
  return nullptr;
}

} // namespace

// -----------------------------------------------------------------------
// ELF
// -----------------------------------------------------------------------

TEST_CASE("loader - parses a minimal ELF64 x86-64 image", "[loader][elf]") {
  std::string err;
  auto img = LoadedImage::loadBytes(toVec(kTinyElfX64, sizeof(kTinyElfX64)), &err);
  REQUIRE(img.has_value());
  CHECK(err.empty());

  CHECK(img->format() == Format::ELF);
  CHECK(img->architecture() == Architecture::X86_64);
  CHECK(img->is64Bit());
  CHECK(img->hasVirtualLayout());

  REQUIRE(img->entryPoint().has_value());
  CHECK(*img->entryPoint() == kTinyElfX64Entry);

  SECTION("the .text section is executable and carries the real bytes") {
    const Section *text = findSection(*img, ".text");
    REQUIRE(text != nullptr);
    CHECK(text->executable);
    CHECK(text->address == kTinyElfX64Entry);
    // push rbp; mov rbp,rsp; xor eax,eax; pop rbp; ret
    REQUIRE(text->bytes.size() == 8);
    CHECK(text->bytes[0] == 0x55);
    CHECK(text->bytes.back() == 0xC3);
  }

  SECTION("the function symbol is recovered at the entry") {
    const Symbol *fn = img->functionAt(kTinyElfX64Entry);
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "myfunc");
    CHECK(fn->kind == SymbolKind::Function);
    CHECK_FALSE(fn->imported);
  }

  SECTION("readVirtual spans the entry and returns the first instruction") {
    auto bytes = img->readVirtual(kTinyElfX64Entry, 4);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 4);
    CHECK((*bytes)[0] == 0x55);
  }

  SECTION("readVirtual rejects an unmapped address") {
    CHECK_FALSE(img->readVirtual(0xDEAD0000, 4).has_value());
  }
}

TEST_CASE("loader - disassembleImage turns an ELF into a populated Graph",
          "[loader][elf][pipeline]") {
  auto img = LoadedImage::loadBytes(toVec(kTinyElfX64, sizeof(kTinyElfX64)));
  REQUIRE(img.has_value());

  Graph g = disassembleImage(*img);
  CHECK(g.architecture() == Architecture::X86_64);
  // Five instructions: push/mov/xor/pop/ret, all inside .text.
  CHECK(g.size() == 5);
  CHECK(g.get(kTinyElfX64Entry) != nullptr);
  CHECK(g.isValidFunction(kTinyElfX64Entry));
}

// -----------------------------------------------------------------------
// PE
// -----------------------------------------------------------------------

TEST_CASE("loader - parses a minimal PE32+ x86-64 image", "[loader][pe]") {
  std::string err;
  auto img = LoadedImage::loadBytes(toVec(kTinyPeX64, sizeof(kTinyPeX64)), &err);
  REQUIRE(img.has_value());
  CHECK(err.empty());

  CHECK(img->format() == Format::PE);
  CHECK(img->architecture() == Architecture::X86_64);
  CHECK(img->is64Bit());
  CHECK(img->imageBase() == 0x140000000ULL);

  REQUIRE(img->entryPoint().has_value());
  CHECK(*img->entryPoint() == kTinyPeX64Entry); // imageBase + 0x1000

  const Section *text = findSection(*img, ".text");
  REQUIRE(text != nullptr);
  CHECK(text->executable);
  CHECK(text->readable);
  CHECK(text->address == kTinyPeX64Entry);
}

// -----------------------------------------------------------------------
// Errors
// -----------------------------------------------------------------------

TEST_CASE("loader - rejects non-object bytes with an error", "[loader]") {
  std::string err;
  auto img = LoadedImage::loadBytes({0x00, 0x01, 0x02, 0x03, 0x04}, &err);
  CHECK_FALSE(img.has_value());
  CHECK_FALSE(err.empty());
}

TEST_CASE("loader - loadFile on a missing path fails cleanly", "[loader]") {
  std::string err;
  auto img = LoadedImage::loadFile("/no/such/file/at/all", &err);
  CHECK_FALSE(img.has_value());
  CHECK_FALSE(err.empty());
}
