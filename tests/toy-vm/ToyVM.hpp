#pragma once

// A tiny, deliberately obfuscated stack-machine VM, hand-written in x86-64
// assembly and assembled via dragon::Assembler - built as the first,
// smallest possible end-to-end validation of dragon-tales' planned
// VM-devirtualization pipeline (see the `vm-devirtualization` and
// `trace-capture` skills under .agents/skills/): capture a real execution
// trace through the interpreter -> Disassembler::liftTrace() -> LLVM IR ->
// real LLVM optimization passes -> does the obfuscation actually collapse.
//
// Every arithmetic/bitwise handler below computes its result via a classic
// mixed-boolean-arithmetic (MBA) identity instead of the native x86
// instruction - the same style of noise the two 2026 back.engineering posts
// describe collapsing with an ordinary instcombine-style fixed-point pass:
//   ADD(a,b) = (a^b) + 2*(a&b)     ["2*x" computed as ADD(x,x) - see below]
//   SUB(a,b) = ADD(a, -b)          [same identity; "-b" computed as 0-b]
//   AND(a,b) = (a|b) - (a^b)
//   OR (a,b) = (a&b) + (a^b)
//   XOR(a,b) = (a|b) - (a&b)
// Nothing here is meant to resist a serious deobfuscator - it's a minimal,
// hand-verifiable fixture to prove the mechanics of the pipeline work
// before pointing it at anything real.
//
// -- Real X86IGNILLifter gaps this exercise found --------------------------
//
// The interpreter was originally written the "obvious" way: a byte-per-
// opcode bytecode stream, an index register, SIB (base+index) addressing,
// INC/DEC for the counters, MOVZX to widen fetched bytes, SHL for the
// MBA identities' "multiply by 2", and NEG for SUB's "-b". Every one of
// those choices turned out to hit a *real*, previously-undocumented gap in
// X86IGNILLifter - each one silently producing UNDEF instead of the real
// computation (the LifterGaps.md #1 failure shape, just for different
// mnemonics), discovered by running exactly this experiment:
//   - INC/DEC are not handled *at all* (any form) - always UNDEF.
//   - MOVZX is not handled *at all* (any form, register or memory operand).
//   - SHL/SHR/SAR by an immediate are not handled *at all* - always UNDEF.
//     (LLVMLifter::liftOp's SHL/SHR/SAR cases were also separately found
//     and fixed to zext/trunc the shift-count operand to the shifted
//     value's width - a real bug in its own right, but it only matters
//     once IGNIL actually emits an SHL/SHR/SAR op in the first place,
//     which this specific form never does.)
//   - NEG is not handled *at all* (any form) - always UNDEF.
//   - 8-bit MOV (load or store) is a gap, consistent with the "8/16-bit
//     forms" gap `scripts/audit_x86_lifter_coverage.py` already tracks for
//     the ALU-family mnemonics, but apparently not limited to those.
//   - Plain MOV/CMP with base+index (SIB, e.g. `[rsi+r8]`) also produced
//     UNDEF even for forms that should be "covered" per
//     ToDo/Roadmap.md - the covered/gap boundary is narrower than that
//     note describes for non-scale-4 SIB forms; not fully root-caused here.
// None of this broke the interpreter itself (Unicorn runs real hardware
// semantics regardless of dragon-tales' coverage) - it broke the *lifted*
// path silently. Two separate, genuine correctness bugs were also found
// and fixed this way:
//   - LLVMLifter::mergeIntoParent preserved the high 32 bits of a 64-bit
//     register on a 32-bit sub-register write, when x86-64 actually
//     zero-extends on that specific transition (e.g. `xor ecx,ecx` must
//     clear all of RCX, not just ECX).
//   - `cmp eax, 0xFFFFFFFF` (and presumably other reg,imm32 forms with the
//     immediate's high bit set) decodes to Const{width=32,
//     value=0xFFFFFFFFFFFFFFFF} - the immediate gets sign-extended to a
//     full 64-bit pattern without masking back down to the declared
//     32-bit width. Harmless in a NDEBUG build (llvm::APInt silently
//     truncates), but `llvm::APInt`'s constructor asserts that the value
//     fits the given bit width, so this reliably SIGABRTs any
//     assertions-enabled build the moment such a Const reaches
//     LLVMLifter::resolve(). Fixed defensively at that one choke point
//     (mask to width before constructing the APInt) rather than chasing
//     down every X86IGNILLifter immediate-decoding call site that might
//     produce one of these - the root cause upstream is still real and
//     un-fixed, this only stops it from crashing downstream.
//
// Rather than extend X86IGNILLifter's dispatch table for INC/DEC/MOVZX/
// SHL-by-imm/NEG in the middle of a from-scratch pipeline experiment, this
// VM was redesigned around instruction forms empirically confirmed to lift
// correctly: 32-bit/64-bit MOV/ADD/SUB with base(+disp)-only addressing, no
// SIB, no INC/DEC, no MOVZX, no SHL, no NEG. See `ToDo/Roadmap.md` for
// these gaps tracked as follow-up work.
//
// Concretely: bytecode is an array of 4-byte little-endian words (not
// packed bytes), and the interpreter advances pointers by 4 with ADD/SUB
// rather than indexing through a separate counter register with SIB
// addressing.

#include <cstdint>
#include <vector>

namespace dragon::test::toyvm {

// -- Bytecode ISA ----------------------------------------------------------
//
// Stack machine, 32-bit value slots, 32-bit opcode/immediate words (see the
// file header for why words rather than bytes). OP_PUSH takes one extra
// word: the immediate value to push, verbatim.
enum Opcode : std::uint32_t {
  OP_PUSH = 1,
  OP_ADD  = 2,
  OP_SUB  = 3,
  OP_AND  = 4,
  OP_OR   = 5,
  OP_XOR  = 6,
  OP_HALT = 0xFFFFFFFFu,
};

// -- The interpreter, in x86-64 assembly ------------------------------------
//
// Register roles (fixed for the whole routine, not saved/restored - this is
// a self-contained routine driven directly by Unicorn, not a normal ABI
// function):
//   rsi = bytecode read pointer (advances by 4 bytes per word fetched)
//   rdi = value-stack pointer (advances by 4 on push, retreats by 4 on pop -
//         points one past the top of stack, like a real stack pointer)
//   rax, rbx, rdx = scratch inside handlers
//
// On OP_HALT, the result (top of stack) is left in eax, then the routine
// returns via a plain `ret`.
inline const char *kInterpreterSource = R"asm(
dispatch:
    mov eax, dword ptr [rsi]
    add rsi, 4
    cmp eax, 1
    je op_push
    cmp eax, 2
    je op_add
    cmp eax, 3
    je op_sub
    cmp eax, 4
    je op_and
    cmp eax, 5
    je op_or
    cmp eax, 6
    je op_xor
    cmp eax, 0xFFFFFFFF
    je op_halt
    jmp dispatch

op_push:
    mov eax, dword ptr [rsi]
    add rsi, 4
    mov dword ptr [rdi], eax
    add rdi, 4
    jmp dispatch

op_add:
    sub rdi, 4
    mov ebx, dword ptr [rdi]
    sub rdi, 4
    mov eax, dword ptr [rdi]
    mov edx, eax
    xor edx, ebx
    and eax, ebx
    add eax, eax
    add eax, edx
    mov dword ptr [rdi], eax
    add rdi, 4
    jmp dispatch

op_sub:
    sub rdi, 4
    mov ebx, dword ptr [rdi]
    sub rdi, 4
    mov eax, dword ptr [rdi]
    mov edx, 0
    sub edx, ebx
    mov ebx, edx
    mov edx, eax
    xor edx, ebx
    and eax, ebx
    add eax, eax
    add eax, edx
    mov dword ptr [rdi], eax
    add rdi, 4
    jmp dispatch

op_and:
    sub rdi, 4
    mov ebx, dword ptr [rdi]
    sub rdi, 4
    mov eax, dword ptr [rdi]
    mov edx, eax
    or edx, ebx
    xor eax, ebx
    sub edx, eax
    mov dword ptr [rdi], edx
    add rdi, 4
    jmp dispatch

op_or:
    sub rdi, 4
    mov ebx, dword ptr [rdi]
    sub rdi, 4
    mov eax, dword ptr [rdi]
    mov edx, eax
    and edx, ebx
    xor eax, ebx
    add edx, eax
    mov dword ptr [rdi], edx
    add rdi, 4
    jmp dispatch

op_xor:
    sub rdi, 4
    mov ebx, dword ptr [rdi]
    sub rdi, 4
    mov eax, dword ptr [rdi]
    mov edx, eax
    or edx, ebx
    and eax, ebx
    sub edx, eax
    mov dword ptr [rdi], edx
    add rdi, 4
    jmp dispatch

op_halt:
    mov eax, dword ptr [rdi - 4]
    ret
)asm";

namespace detail {
inline void pushWord(std::vector<std::uint8_t> &bytes, std::uint32_t word) {
  bytes.push_back(static_cast<std::uint8_t>(word));
  bytes.push_back(static_cast<std::uint8_t>(word >> 8));
  bytes.push_back(static_cast<std::uint8_t>(word >> 16));
  bytes.push_back(static_cast<std::uint8_t>(word >> 24));
}
} // namespace detail

// -- Sample bytecode program -------------------------------------------------
//
// Computes ((a + b) ^ (a & c)) | (b - c) for a=10, b=4, c=3:
//   a+b = 14
//   a&c = 2
//   14^2 = 12
//   b-c = 1
//   12|1 = 13
// Exercises every obfuscated handler at least once. Expected result: 13.
inline std::vector<std::uint8_t> program() {
  std::vector<std::uint8_t> bytes;
  auto emit = [&](std::uint32_t w) { detail::pushWord(bytes, w); };

  emit(OP_PUSH); emit(10); // a
  emit(OP_PUSH); emit(4);  // b
  emit(OP_ADD);            // -> 14
  emit(OP_PUSH); emit(10); // a
  emit(OP_PUSH); emit(3);  // c
  emit(OP_AND);            // -> 2
  emit(OP_XOR);            // -> 12
  emit(OP_PUSH); emit(4);  // b
  emit(OP_PUSH); emit(3);  // c
  emit(OP_SUB);            // -> 1
  emit(OP_OR);             // -> 13
  emit(OP_HALT);

  return bytes;
}

inline constexpr std::uint32_t kExpectedResult = 13;

// -- Memory layout (shared by the Unicorn run and the JIT-executed lift) ----
inline constexpr std::uint64_t kCodeBase        = 0x1000;
inline constexpr std::uint64_t kBytecodeBase    = 0x3000;
inline constexpr std::uint64_t kStackBase       = 0x4000;
inline constexpr std::uint64_t kNativeStackBase = 0x5000;
inline constexpr std::uint64_t kPageSize        = 0x1000;
// Sentinel PC the interpreter's `ret` is made to land on so Unicorn has a
// well-defined stopping point - see X86SemanticRoundTripFixture.h for the
// same trick applied to a normal function.
inline constexpr std::uint64_t kReturnSentinel = 0x7fff0000ULL;

// IGNIL register-address-space offsets for the registers this interpreter
// uses (must match lib/disassemblers/x86/x86registers.def).
namespace regoff {
inline constexpr std::uint32_t RAX = 0, RDX = 16, RBX = 24;
inline constexpr std::uint32_t RSI = 48, RDI = 56;
} // namespace regoff

} // namespace dragon::test::toyvm
