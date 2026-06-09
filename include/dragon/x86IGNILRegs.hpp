#pragma once

#include <dragon/IGNIL.hpp>

/// IGNIL register descriptors for the x86 / x86-64 architecture.
///
/// Every architectural register is represented as an ignil::Reg value:
///   - `offset` is its byte position in the IGNIL register address space.
///   - `width`  is the bit width.
///
/// Sub-registers share the same base offset as their parent 64-bit register,
/// but carry a smaller width:
///
///   RAX  Reg{0,64}   EAX Reg{0,32}   AX Reg{0,16}   AL Reg{0,8}   AH Reg{1,8}
///
/// Layout (one slot = 8 bytes):
///   RAX RCX RDX RBX RSP RBP RSI RDI R8..R15  RIP  CF PF AF ZF SF OF DF
///     0   8  16  24  32  40  48  56   64-120  128 136 ...              142
///
/// Flag registers are 1 bit wide and stored at consecutive byte offsets
/// starting at 136.

namespace dragon::x86 {

// ---------------------------------------------------------------------------
// Offset constants — use these if you need the raw byte offset.
// ---------------------------------------------------------------------------

inline constexpr ignil::Width k8 = 8;
inline constexpr ignil::Width k16 = 16;
inline constexpr ignil::Width k32 = 32;
inline constexpr ignil::Width k64 = 64;

inline constexpr std::uint32_t kOffRAX = 0;
inline constexpr std::uint32_t kOffRCX = 8;
inline constexpr std::uint32_t kOffRDX = 16;
inline constexpr std::uint32_t kOffRBX = 24;
inline constexpr std::uint32_t kOffRSP = 32;
inline constexpr std::uint32_t kOffRBP = 40;
inline constexpr std::uint32_t kOffRSI = 48;
inline constexpr std::uint32_t kOffRDI = 56;
inline constexpr std::uint32_t kOffR8 = 64;
inline constexpr std::uint32_t kOffR9 = 72;
inline constexpr std::uint32_t kOffR10 = 80;
inline constexpr std::uint32_t kOffR11 = 88;
inline constexpr std::uint32_t kOffR12 = 96;
inline constexpr std::uint32_t kOffR13 = 104;
inline constexpr std::uint32_t kOffR14 = 112;
inline constexpr std::uint32_t kOffR15 = 120;
inline constexpr std::uint32_t kOffRIP = 128;

inline constexpr std::uint32_t kOffCF = 136;
inline constexpr std::uint32_t kOffPF = 137;
inline constexpr std::uint32_t kOffAF = 138;
inline constexpr std::uint32_t kOffZF = 139;
inline constexpr std::uint32_t kOffSF = 140;
inline constexpr std::uint32_t kOffOF = 141;
inline constexpr std::uint32_t kOffDF = 142;

// ---------------------------------------------------------------------------
// 64-bit general-purpose registers
// ---------------------------------------------------------------------------

inline constexpr ignil::Reg RAX{kOffRAX, k64};
inline constexpr ignil::Reg RCX{kOffRCX, k64};
inline constexpr ignil::Reg RDX{kOffRDX, k64};
inline constexpr ignil::Reg RBX{kOffRBX, k64};
inline constexpr ignil::Reg RSP{kOffRSP, k64};
inline constexpr ignil::Reg RBP{kOffRBP, k64};
inline constexpr ignil::Reg RSI{kOffRSI, k64};
inline constexpr ignil::Reg RDI{kOffRDI, k64};
inline constexpr ignil::Reg R8{kOffR8, k64};
inline constexpr ignil::Reg R9{kOffR9, k64};
inline constexpr ignil::Reg R10{kOffR10, k64};
inline constexpr ignil::Reg R11{kOffR11, k64};
inline constexpr ignil::Reg R12{kOffR12, k64};
inline constexpr ignil::Reg R13{kOffR13, k64};
inline constexpr ignil::Reg R14{kOffR14, k64};
inline constexpr ignil::Reg R15{kOffR15, k64};
inline constexpr ignil::Reg RIP{kOffRIP, k64};

// ---------------------------------------------------------------------------
// 32-bit general-purpose registers
// ---------------------------------------------------------------------------

inline constexpr ignil::Reg EAX{kOffRAX, k32};
inline constexpr ignil::Reg ECX{kOffRCX, k32};
inline constexpr ignil::Reg EDX{kOffRDX, k32};
inline constexpr ignil::Reg EBX{kOffRBX, k32};
inline constexpr ignil::Reg ESP{kOffRSP, k32};
inline constexpr ignil::Reg EBP{kOffRBP, k32};
inline constexpr ignil::Reg ESI{kOffRSI, k32};
inline constexpr ignil::Reg EDI{kOffRDI, k32};
inline constexpr ignil::Reg R8D{kOffR8, k32};
inline constexpr ignil::Reg R9D{kOffR9, k32};
inline constexpr ignil::Reg R10D{kOffR10, k32};
inline constexpr ignil::Reg R11D{kOffR11, k32};
inline constexpr ignil::Reg R12D{kOffR12, k32};
inline constexpr ignil::Reg R13D{kOffR13, k32};
inline constexpr ignil::Reg R14D{kOffR14, k32};
inline constexpr ignil::Reg R15D{kOffR15, k32};

// ---------------------------------------------------------------------------
// 16-bit general-purpose registers
// ---------------------------------------------------------------------------

inline constexpr ignil::Reg AX{kOffRAX, k16};
inline constexpr ignil::Reg CX{kOffRCX, k16};
inline constexpr ignil::Reg DX{kOffRDX, k16};
inline constexpr ignil::Reg BX{kOffRBX, k16};
inline constexpr ignil::Reg SP{kOffRSP, k16};
inline constexpr ignil::Reg BP{kOffRBP, k16};
inline constexpr ignil::Reg SI{kOffRSI, k16};
inline constexpr ignil::Reg DI{kOffRDI, k16};
inline constexpr ignil::Reg R8W{kOffR8, k16};
inline constexpr ignil::Reg R9W{kOffR9, k16};
inline constexpr ignil::Reg R10W{kOffR10, k16};
inline constexpr ignil::Reg R11W{kOffR11, k16};
inline constexpr ignil::Reg R12W{kOffR12, k16};
inline constexpr ignil::Reg R13W{kOffR13, k16};
inline constexpr ignil::Reg R14W{kOffR14, k16};
inline constexpr ignil::Reg R15W{kOffR15, k16};

// ---------------------------------------------------------------------------
// 8-bit low general-purpose registers
// ---------------------------------------------------------------------------

inline constexpr ignil::Reg AL{kOffRAX, k8};
inline constexpr ignil::Reg CL{kOffRCX, k8};
inline constexpr ignil::Reg DL{kOffRDX, k8};
inline constexpr ignil::Reg BL{kOffRBX, k8};
inline constexpr ignil::Reg SPL{kOffRSP, k8};
inline constexpr ignil::Reg BPL{kOffRBP, k8};
inline constexpr ignil::Reg SIL{kOffRSI, k8};
inline constexpr ignil::Reg DIL{kOffRDI, k8};
inline constexpr ignil::Reg R8B{kOffR8, k8};
inline constexpr ignil::Reg R9B{kOffR9, k8};
inline constexpr ignil::Reg R10B{kOffR10, k8};
inline constexpr ignil::Reg R11B{kOffR11, k8};
inline constexpr ignil::Reg R12B{kOffR12, k8};
inline constexpr ignil::Reg R13B{kOffR13, k8};
inline constexpr ignil::Reg R14B{kOffR14, k8};
inline constexpr ignil::Reg R15B{kOffR15, k8};

// ---------------------------------------------------------------------------
// 8-bit high registers (byte 1 of the parent 64-bit register).
// Only available for RAX, RCX, RDX, RBX — the legacy high-byte aliases.
// ---------------------------------------------------------------------------

inline constexpr ignil::Reg AH{kOffRAX + 1, k8};
inline constexpr ignil::Reg CH{kOffRCX + 1, k8};
inline constexpr ignil::Reg DH{kOffRDX + 1, k8};
inline constexpr ignil::Reg BH{kOffRBX + 1, k8};

// ---------------------------------------------------------------------------
// EFLAGS — 8-bit wide in the IGNIL register space (one byte slot each).
// The boolean value stored inside is still 1 bit; the register container
// is 8 bits as defined in x86registers.def.
// ---------------------------------------------------------------------------

inline constexpr ignil::Reg CF{kOffCF, k8}; ///< Carry flag
inline constexpr ignil::Reg PF{kOffPF, k8}; ///< Parity flag
inline constexpr ignil::Reg AF{kOffAF, k8}; ///< Auxiliary carry flag
inline constexpr ignil::Reg ZF{kOffZF, k8}; ///< Zero flag
inline constexpr ignil::Reg SF{kOffSF, k8}; ///< Sign flag
inline constexpr ignil::Reg OF{kOffOF, k8}; ///< Overflow flag
inline constexpr ignil::Reg DF{kOffDF, k8}; ///< Direction flag

} // namespace dragon::x86
