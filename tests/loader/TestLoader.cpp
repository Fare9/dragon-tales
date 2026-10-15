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

/// The tiny ELF with its section header table removed (e_shoff, e_shnum and
/// e_shstrndx zeroed), the way VMProtect and similar rewriters emit it: only
/// the PT_LOAD program headers say what gets mapped.
std::vector<std::uint8_t> tinyElfWithoutSections() {
  std::vector<std::uint8_t> v = toVec(kTinyElfX64, sizeof(kTinyElfX64));
  std::fill(v.begin() + 0x28, v.begin() + 0x30, 0); // e_shoff
  std::fill(v.begin() + 0x3C, v.begin() + 0x40, 0); // e_shnum, e_shstrndx
  return v;
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

TEST_CASE("loader - ELF image base is the lowest PT_LOAD address",
          "[loader][elf]") {
  auto img = LoadedImage::loadBytes(toVec(kTinyElfX64, sizeof(kTinyElfX64)));
  REQUIRE(img.has_value());

  // The fixture's PT_LOAD maps the file from 0x400000; .text only starts at
  // 0x400078, so a section-based guess would get the base wrong.
  CHECK(img->imageBase() == 0x400000ULL);

  REQUIRE(img->segments().size() == 1);
  const Section &seg = img->segments().front();
  CHECK(seg.address == 0x400000ULL);
  CHECK(seg.readable);
  CHECK(seg.executable);
  CHECK_FALSE(seg.writable);
}

TEST_CASE("loader - a section-less ELF is mapped from its PT_LOAD segments",
          "[loader][elf]") {
  std::string err;
  auto img = LoadedImage::loadBytes(tinyElfWithoutSections(), &err);
  REQUIRE(img.has_value());
  CHECK(err.empty());

  CHECK(img->format() == Format::ELF);
  CHECK(img->architecture() == Architecture::X86_64);
  CHECK(img->sections().empty());
  CHECK(img->hasVirtualLayout());
  CHECK(img->imageBase() == 0x400000ULL);
  REQUIRE(img->entryPoint().has_value());
  CHECK(*img->entryPoint() == kTinyElfX64Entry);

  SECTION("readVirtual reaches code and headers through the segment") {
    auto code = img->readVirtual(kTinyElfX64Entry, 8);
    REQUIRE(code.has_value());
    CHECK(*code == std::vector<std::uint8_t>{0x55, 0x48, 0x89, 0xE5, 0x31,
                                             0xC0, 0x5D, 0xC3});
    auto magic = img->readVirtual(0x400000ULL, 4);
    REQUIRE(magic.has_value());
    CHECK(*magic == std::vector<std::uint8_t>{0x7F, 'E', 'L', 'F'});
    CHECK_FALSE(img->readVirtual(0x500000ULL, 1).has_value());
  }

  SECTION("the executable segment stands in for the missing .text") {
    auto exec = img->executableSections();
    REQUIRE(exec.size() == 1);
    CHECK(exec.front()->address == 0x400000ULL);
    CHECK(exec.front()->contains(kTinyElfX64Entry));
  }

  SECTION("disassembleImage decodes the entry function") {
    Graph g = disassembleImage(*img);
    CHECK(g.architecture() == Architecture::X86_64);
    CHECK(g.get(kTinyElfX64Entry) != nullptr);
    CHECK(g.isValidFunction(kTinyElfX64Entry));
  }
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
