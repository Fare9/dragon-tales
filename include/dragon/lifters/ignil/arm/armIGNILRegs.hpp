#pragma once

#include <dragon/lifters/ignil/IGNIL.hpp>

/// IGNIL register descriptors for AArch64.
///
/// Every architectural register is an ignil::Reg:
///   offset — byte position in the IGNIL register address space.
///   width  — bit width.
///   name   — human-readable name used in output.
///
/// Sub-registers share the base offset of their parent and carry a smaller
/// width, matching the AArch64 view model:
///
///   X0  Reg{0,64,"X0"}   W0  Reg{0,32,"W0"}
///   V0  Reg{272,128,"V0"}  Q0  Reg{272,128,"Q0"}
///   D0  Reg{272,64,"D0"}   S0  Reg{272,32,"S0"}
///
/// Layout summary:
///   X0..X28  X29(FP)  X30(LR)  XZR  SP   PC   (8 bytes each)
///     0..224    232      240    248  256  264
///
///   V0..V31  (16 bytes / 128-bit each)
///     272..768
///
///   NF  ZF  CF  VF   (NZCV, 1 byte each)
///   784 785 786 787

namespace dragon::arm {

// ---------------------------------------------------------------------------
// Width constants
// ---------------------------------------------------------------------------

inline constexpr ignil::Width k8   =   8;
inline constexpr ignil::Width k16  =  16;
inline constexpr ignil::Width k32  =  32;
inline constexpr ignil::Width k64  =  64;
inline constexpr ignil::Width k128 = 128;

// ---------------------------------------------------------------------------
// Offset constants — general-purpose
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kOffX0  =   0;
inline constexpr std::uint32_t kOffX1  =   8;
inline constexpr std::uint32_t kOffX2  =  16;
inline constexpr std::uint32_t kOffX3  =  24;
inline constexpr std::uint32_t kOffX4  =  32;
inline constexpr std::uint32_t kOffX5  =  40;
inline constexpr std::uint32_t kOffX6  =  48;
inline constexpr std::uint32_t kOffX7  =  56;
inline constexpr std::uint32_t kOffX8  =  64;
inline constexpr std::uint32_t kOffX9  =  72;
inline constexpr std::uint32_t kOffX10 =  80;
inline constexpr std::uint32_t kOffX11 =  88;
inline constexpr std::uint32_t kOffX12 =  96;
inline constexpr std::uint32_t kOffX13 = 104;
inline constexpr std::uint32_t kOffX14 = 112;
inline constexpr std::uint32_t kOffX15 = 120;
inline constexpr std::uint32_t kOffX16 = 128;
inline constexpr std::uint32_t kOffX17 = 136;
inline constexpr std::uint32_t kOffX18 = 144;
inline constexpr std::uint32_t kOffX19 = 152;
inline constexpr std::uint32_t kOffX20 = 160;
inline constexpr std::uint32_t kOffX21 = 168;
inline constexpr std::uint32_t kOffX22 = 176;
inline constexpr std::uint32_t kOffX23 = 184;
inline constexpr std::uint32_t kOffX24 = 192;
inline constexpr std::uint32_t kOffX25 = 200;
inline constexpr std::uint32_t kOffX26 = 208;
inline constexpr std::uint32_t kOffX27 = 216;
inline constexpr std::uint32_t kOffX28 = 224;
inline constexpr std::uint32_t kOffX29 = 232; // FP
inline constexpr std::uint32_t kOffX30 = 240; // LR
inline constexpr std::uint32_t kOffXZR = 248;
inline constexpr std::uint32_t kOffSP  = 256;
inline constexpr std::uint32_t kOffPC  = 264;

// ---------------------------------------------------------------------------
// Offset constants — NEON/FP (16 bytes per slot)
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kOffV0  = 272;
inline constexpr std::uint32_t kOffV1  = 288;
inline constexpr std::uint32_t kOffV2  = 304;
inline constexpr std::uint32_t kOffV3  = 320;
inline constexpr std::uint32_t kOffV4  = 336;
inline constexpr std::uint32_t kOffV5  = 352;
inline constexpr std::uint32_t kOffV6  = 368;
inline constexpr std::uint32_t kOffV7  = 384;
inline constexpr std::uint32_t kOffV8  = 400;
inline constexpr std::uint32_t kOffV9  = 416;
inline constexpr std::uint32_t kOffV10 = 432;
inline constexpr std::uint32_t kOffV11 = 448;
inline constexpr std::uint32_t kOffV12 = 464;
inline constexpr std::uint32_t kOffV13 = 480;
inline constexpr std::uint32_t kOffV14 = 496;
inline constexpr std::uint32_t kOffV15 = 512;
inline constexpr std::uint32_t kOffV16 = 528;
inline constexpr std::uint32_t kOffV17 = 544;
inline constexpr std::uint32_t kOffV18 = 560;
inline constexpr std::uint32_t kOffV19 = 576;
inline constexpr std::uint32_t kOffV20 = 592;
inline constexpr std::uint32_t kOffV21 = 608;
inline constexpr std::uint32_t kOffV22 = 624;
inline constexpr std::uint32_t kOffV23 = 640;
inline constexpr std::uint32_t kOffV24 = 656;
inline constexpr std::uint32_t kOffV25 = 672;
inline constexpr std::uint32_t kOffV26 = 688;
inline constexpr std::uint32_t kOffV27 = 704;
inline constexpr std::uint32_t kOffV28 = 720;
inline constexpr std::uint32_t kOffV29 = 736;
inline constexpr std::uint32_t kOffV30 = 752;
inline constexpr std::uint32_t kOffV31 = 768;

// ---------------------------------------------------------------------------
// Offset constants — NZCV flags
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kOffNF = 784;
inline constexpr std::uint32_t kOffZF = 785;
inline constexpr std::uint32_t kOffCF = 786;
inline constexpr std::uint32_t kOffVF = 787;

// ---------------------------------------------------------------------------
// 64-bit general-purpose registers
// ---------------------------------------------------------------------------

inline const ignil::Reg X0 {kOffX0,  k64, "X0"};
inline const ignil::Reg X1 {kOffX1,  k64, "X1"};
inline const ignil::Reg X2 {kOffX2,  k64, "X2"};
inline const ignil::Reg X3 {kOffX3,  k64, "X3"};
inline const ignil::Reg X4 {kOffX4,  k64, "X4"};
inline const ignil::Reg X5 {kOffX5,  k64, "X5"};
inline const ignil::Reg X6 {kOffX6,  k64, "X6"};
inline const ignil::Reg X7 {kOffX7,  k64, "X7"};
inline const ignil::Reg X8 {kOffX8,  k64, "X8"};
inline const ignil::Reg X9 {kOffX9,  k64, "X9"};
inline const ignil::Reg X10{kOffX10, k64, "X10"};
inline const ignil::Reg X11{kOffX11, k64, "X11"};
inline const ignil::Reg X12{kOffX12, k64, "X12"};
inline const ignil::Reg X13{kOffX13, k64, "X13"};
inline const ignil::Reg X14{kOffX14, k64, "X14"};
inline const ignil::Reg X15{kOffX15, k64, "X15"};
inline const ignil::Reg X16{kOffX16, k64, "X16"};
inline const ignil::Reg X17{kOffX17, k64, "X17"};
inline const ignil::Reg X18{kOffX18, k64, "X18"};
inline const ignil::Reg X19{kOffX19, k64, "X19"};
inline const ignil::Reg X20{kOffX20, k64, "X20"};
inline const ignil::Reg X21{kOffX21, k64, "X21"};
inline const ignil::Reg X22{kOffX22, k64, "X22"};
inline const ignil::Reg X23{kOffX23, k64, "X23"};
inline const ignil::Reg X24{kOffX24, k64, "X24"};
inline const ignil::Reg X25{kOffX25, k64, "X25"};
inline const ignil::Reg X26{kOffX26, k64, "X26"};
inline const ignil::Reg X27{kOffX27, k64, "X27"};
inline const ignil::Reg X28{kOffX28, k64, "X28"};
inline const ignil::Reg X29{kOffX29, k64, "X29"}; // frame pointer
inline const ignil::Reg X30{kOffX30, k64, "X30"}; // link register
inline const ignil::Reg XZR{kOffXZR, k64, "XZR"};
inline const ignil::Reg SP {kOffSP,  k64, "SP"};
inline const ignil::Reg PC {kOffPC,  k64, "PC"};

// Aliases
inline const ignil::Reg FP{kOffX29, k64, "FP"};
inline const ignil::Reg LR{kOffX30, k64, "LR"};

// ---------------------------------------------------------------------------
// 32-bit general-purpose registers (lower half of each X register)
// ---------------------------------------------------------------------------

inline const ignil::Reg W0 {kOffX0,  k32, "W0"};
inline const ignil::Reg W1 {kOffX1,  k32, "W1"};
inline const ignil::Reg W2 {kOffX2,  k32, "W2"};
inline const ignil::Reg W3 {kOffX3,  k32, "W3"};
inline const ignil::Reg W4 {kOffX4,  k32, "W4"};
inline const ignil::Reg W5 {kOffX5,  k32, "W5"};
inline const ignil::Reg W6 {kOffX6,  k32, "W6"};
inline const ignil::Reg W7 {kOffX7,  k32, "W7"};
inline const ignil::Reg W8 {kOffX8,  k32, "W8"};
inline const ignil::Reg W9 {kOffX9,  k32, "W9"};
inline const ignil::Reg W10{kOffX10, k32, "W10"};
inline const ignil::Reg W11{kOffX11, k32, "W11"};
inline const ignil::Reg W12{kOffX12, k32, "W12"};
inline const ignil::Reg W13{kOffX13, k32, "W13"};
inline const ignil::Reg W14{kOffX14, k32, "W14"};
inline const ignil::Reg W15{kOffX15, k32, "W15"};
inline const ignil::Reg W16{kOffX16, k32, "W16"};
inline const ignil::Reg W17{kOffX17, k32, "W17"};
inline const ignil::Reg W18{kOffX18, k32, "W18"};
inline const ignil::Reg W19{kOffX19, k32, "W19"};
inline const ignil::Reg W20{kOffX20, k32, "W20"};
inline const ignil::Reg W21{kOffX21, k32, "W21"};
inline const ignil::Reg W22{kOffX22, k32, "W22"};
inline const ignil::Reg W23{kOffX23, k32, "W23"};
inline const ignil::Reg W24{kOffX24, k32, "W24"};
inline const ignil::Reg W25{kOffX25, k32, "W25"};
inline const ignil::Reg W26{kOffX26, k32, "W26"};
inline const ignil::Reg W27{kOffX27, k32, "W27"};
inline const ignil::Reg W28{kOffX28, k32, "W28"};
inline const ignil::Reg W29{kOffX29, k32, "W29"};
inline const ignil::Reg W30{kOffX30, k32, "W30"};
inline const ignil::Reg WZR{kOffXZR, k32, "WZR"};
inline const ignil::Reg WSP{kOffSP,  k32, "WSP"};

// ---------------------------------------------------------------------------
// NEON/FP vector registers — 128-bit (V and Q share offset; sub-registers
// D/S/H/B select progressively smaller slices of the same slot)
// ---------------------------------------------------------------------------

inline const ignil::Reg V0 {kOffV0,  k128, "V0"};
inline const ignil::Reg V1 {kOffV1,  k128, "V1"};
inline const ignil::Reg V2 {kOffV2,  k128, "V2"};
inline const ignil::Reg V3 {kOffV3,  k128, "V3"};
inline const ignil::Reg V4 {kOffV4,  k128, "V4"};
inline const ignil::Reg V5 {kOffV5,  k128, "V5"};
inline const ignil::Reg V6 {kOffV6,  k128, "V6"};
inline const ignil::Reg V7 {kOffV7,  k128, "V7"};
inline const ignil::Reg V8 {kOffV8,  k128, "V8"};
inline const ignil::Reg V9 {kOffV9,  k128, "V9"};
inline const ignil::Reg V10{kOffV10, k128, "V10"};
inline const ignil::Reg V11{kOffV11, k128, "V11"};
inline const ignil::Reg V12{kOffV12, k128, "V12"};
inline const ignil::Reg V13{kOffV13, k128, "V13"};
inline const ignil::Reg V14{kOffV14, k128, "V14"};
inline const ignil::Reg V15{kOffV15, k128, "V15"};
inline const ignil::Reg V16{kOffV16, k128, "V16"};
inline const ignil::Reg V17{kOffV17, k128, "V17"};
inline const ignil::Reg V18{kOffV18, k128, "V18"};
inline const ignil::Reg V19{kOffV19, k128, "V19"};
inline const ignil::Reg V20{kOffV20, k128, "V20"};
inline const ignil::Reg V21{kOffV21, k128, "V21"};
inline const ignil::Reg V22{kOffV22, k128, "V22"};
inline const ignil::Reg V23{kOffV23, k128, "V23"};
inline const ignil::Reg V24{kOffV24, k128, "V24"};
inline const ignil::Reg V25{kOffV25, k128, "V25"};
inline const ignil::Reg V26{kOffV26, k128, "V26"};
inline const ignil::Reg V27{kOffV27, k128, "V27"};
inline const ignil::Reg V28{kOffV28, k128, "V28"};
inline const ignil::Reg V29{kOffV29, k128, "V29"};
inline const ignil::Reg V30{kOffV30, k128, "V30"};
inline const ignil::Reg V31{kOffV31, k128, "V31"};

inline const ignil::Reg Q0 {kOffV0,  k128, "Q0"};
inline const ignil::Reg Q1 {kOffV1,  k128, "Q1"};
inline const ignil::Reg Q2 {kOffV2,  k128, "Q2"};
inline const ignil::Reg Q3 {kOffV3,  k128, "Q3"};
inline const ignil::Reg Q4 {kOffV4,  k128, "Q4"};
inline const ignil::Reg Q5 {kOffV5,  k128, "Q5"};
inline const ignil::Reg Q6 {kOffV6,  k128, "Q6"};
inline const ignil::Reg Q7 {kOffV7,  k128, "Q7"};
inline const ignil::Reg Q8 {kOffV8,  k128, "Q8"};
inline const ignil::Reg Q9 {kOffV9,  k128, "Q9"};
inline const ignil::Reg Q10{kOffV10, k128, "Q10"};
inline const ignil::Reg Q11{kOffV11, k128, "Q11"};
inline const ignil::Reg Q12{kOffV12, k128, "Q12"};
inline const ignil::Reg Q13{kOffV13, k128, "Q13"};
inline const ignil::Reg Q14{kOffV14, k128, "Q14"};
inline const ignil::Reg Q15{kOffV15, k128, "Q15"};
inline const ignil::Reg Q16{kOffV16, k128, "Q16"};
inline const ignil::Reg Q17{kOffV17, k128, "Q17"};
inline const ignil::Reg Q18{kOffV18, k128, "Q18"};
inline const ignil::Reg Q19{kOffV19, k128, "Q19"};
inline const ignil::Reg Q20{kOffV20, k128, "Q20"};
inline const ignil::Reg Q21{kOffV21, k128, "Q21"};
inline const ignil::Reg Q22{kOffV22, k128, "Q22"};
inline const ignil::Reg Q23{kOffV23, k128, "Q23"};
inline const ignil::Reg Q24{kOffV24, k128, "Q24"};
inline const ignil::Reg Q25{kOffV25, k128, "Q25"};
inline const ignil::Reg Q26{kOffV26, k128, "Q26"};
inline const ignil::Reg Q27{kOffV27, k128, "Q27"};
inline const ignil::Reg Q28{kOffV28, k128, "Q28"};
inline const ignil::Reg Q29{kOffV29, k128, "Q29"};
inline const ignil::Reg Q30{kOffV30, k128, "Q30"};
inline const ignil::Reg Q31{kOffV31, k128, "Q31"};

inline const ignil::Reg D0 {kOffV0,  k64, "D0"};
inline const ignil::Reg D1 {kOffV1,  k64, "D1"};
inline const ignil::Reg D2 {kOffV2,  k64, "D2"};
inline const ignil::Reg D3 {kOffV3,  k64, "D3"};
inline const ignil::Reg D4 {kOffV4,  k64, "D4"};
inline const ignil::Reg D5 {kOffV5,  k64, "D5"};
inline const ignil::Reg D6 {kOffV6,  k64, "D6"};
inline const ignil::Reg D7 {kOffV7,  k64, "D7"};
inline const ignil::Reg D8 {kOffV8,  k64, "D8"};
inline const ignil::Reg D9 {kOffV9,  k64, "D9"};
inline const ignil::Reg D10{kOffV10, k64, "D10"};
inline const ignil::Reg D11{kOffV11, k64, "D11"};
inline const ignil::Reg D12{kOffV12, k64, "D12"};
inline const ignil::Reg D13{kOffV13, k64, "D13"};
inline const ignil::Reg D14{kOffV14, k64, "D14"};
inline const ignil::Reg D15{kOffV15, k64, "D15"};
inline const ignil::Reg D16{kOffV16, k64, "D16"};
inline const ignil::Reg D17{kOffV17, k64, "D17"};
inline const ignil::Reg D18{kOffV18, k64, "D18"};
inline const ignil::Reg D19{kOffV19, k64, "D19"};
inline const ignil::Reg D20{kOffV20, k64, "D20"};
inline const ignil::Reg D21{kOffV21, k64, "D21"};
inline const ignil::Reg D22{kOffV22, k64, "D22"};
inline const ignil::Reg D23{kOffV23, k64, "D23"};
inline const ignil::Reg D24{kOffV24, k64, "D24"};
inline const ignil::Reg D25{kOffV25, k64, "D25"};
inline const ignil::Reg D26{kOffV26, k64, "D26"};
inline const ignil::Reg D27{kOffV27, k64, "D27"};
inline const ignil::Reg D28{kOffV28, k64, "D28"};
inline const ignil::Reg D29{kOffV29, k64, "D29"};
inline const ignil::Reg D30{kOffV30, k64, "D30"};
inline const ignil::Reg D31{kOffV31, k64, "D31"};

inline const ignil::Reg S0 {kOffV0,  k32, "S0"};
inline const ignil::Reg S1 {kOffV1,  k32, "S1"};
inline const ignil::Reg S2 {kOffV2,  k32, "S2"};
inline const ignil::Reg S3 {kOffV3,  k32, "S3"};
inline const ignil::Reg S4 {kOffV4,  k32, "S4"};
inline const ignil::Reg S5 {kOffV5,  k32, "S5"};
inline const ignil::Reg S6 {kOffV6,  k32, "S6"};
inline const ignil::Reg S7 {kOffV7,  k32, "S7"};
inline const ignil::Reg S8 {kOffV8,  k32, "S8"};
inline const ignil::Reg S9 {kOffV9,  k32, "S9"};
inline const ignil::Reg S10{kOffV10, k32, "S10"};
inline const ignil::Reg S11{kOffV11, k32, "S11"};
inline const ignil::Reg S12{kOffV12, k32, "S12"};
inline const ignil::Reg S13{kOffV13, k32, "S13"};
inline const ignil::Reg S14{kOffV14, k32, "S14"};
inline const ignil::Reg S15{kOffV15, k32, "S15"};
inline const ignil::Reg S16{kOffV16, k32, "S16"};
inline const ignil::Reg S17{kOffV17, k32, "S17"};
inline const ignil::Reg S18{kOffV18, k32, "S18"};
inline const ignil::Reg S19{kOffV19, k32, "S19"};
inline const ignil::Reg S20{kOffV20, k32, "S20"};
inline const ignil::Reg S21{kOffV21, k32, "S21"};
inline const ignil::Reg S22{kOffV22, k32, "S22"};
inline const ignil::Reg S23{kOffV23, k32, "S23"};
inline const ignil::Reg S24{kOffV24, k32, "S24"};
inline const ignil::Reg S25{kOffV25, k32, "S25"};
inline const ignil::Reg S26{kOffV26, k32, "S26"};
inline const ignil::Reg S27{kOffV27, k32, "S27"};
inline const ignil::Reg S28{kOffV28, k32, "S28"};
inline const ignil::Reg S29{kOffV29, k32, "S29"};
inline const ignil::Reg S30{kOffV30, k32, "S30"};
inline const ignil::Reg S31{kOffV31, k32, "S31"};

inline const ignil::Reg H0 {kOffV0,  k16, "H0"};
inline const ignil::Reg H1 {kOffV1,  k16, "H1"};
inline const ignil::Reg H2 {kOffV2,  k16, "H2"};
inline const ignil::Reg H3 {kOffV3,  k16, "H3"};
inline const ignil::Reg H4 {kOffV4,  k16, "H4"};
inline const ignil::Reg H5 {kOffV5,  k16, "H5"};
inline const ignil::Reg H6 {kOffV6,  k16, "H6"};
inline const ignil::Reg H7 {kOffV7,  k16, "H7"};
inline const ignil::Reg H8 {kOffV8,  k16, "H8"};
inline const ignil::Reg H9 {kOffV9,  k16, "H9"};
inline const ignil::Reg H10{kOffV10, k16, "H10"};
inline const ignil::Reg H11{kOffV11, k16, "H11"};
inline const ignil::Reg H12{kOffV12, k16, "H12"};
inline const ignil::Reg H13{kOffV13, k16, "H13"};
inline const ignil::Reg H14{kOffV14, k16, "H14"};
inline const ignil::Reg H15{kOffV15, k16, "H15"};
inline const ignil::Reg H16{kOffV16, k16, "H16"};
inline const ignil::Reg H17{kOffV17, k16, "H17"};
inline const ignil::Reg H18{kOffV18, k16, "H18"};
inline const ignil::Reg H19{kOffV19, k16, "H19"};
inline const ignil::Reg H20{kOffV20, k16, "H20"};
inline const ignil::Reg H21{kOffV21, k16, "H21"};
inline const ignil::Reg H22{kOffV22, k16, "H22"};
inline const ignil::Reg H23{kOffV23, k16, "H23"};
inline const ignil::Reg H24{kOffV24, k16, "H24"};
inline const ignil::Reg H25{kOffV25, k16, "H25"};
inline const ignil::Reg H26{kOffV26, k16, "H26"};
inline const ignil::Reg H27{kOffV27, k16, "H27"};
inline const ignil::Reg H28{kOffV28, k16, "H28"};
inline const ignil::Reg H29{kOffV29, k16, "H29"};
inline const ignil::Reg H30{kOffV30, k16, "H30"};
inline const ignil::Reg H31{kOffV31, k16, "H31"};

inline const ignil::Reg B0 {kOffV0,  k8, "B0"};
inline const ignil::Reg B1 {kOffV1,  k8, "B1"};
inline const ignil::Reg B2 {kOffV2,  k8, "B2"};
inline const ignil::Reg B3 {kOffV3,  k8, "B3"};
inline const ignil::Reg B4 {kOffV4,  k8, "B4"};
inline const ignil::Reg B5 {kOffV5,  k8, "B5"};
inline const ignil::Reg B6 {kOffV6,  k8, "B6"};
inline const ignil::Reg B7 {kOffV7,  k8, "B7"};
inline const ignil::Reg B8 {kOffV8,  k8, "B8"};
inline const ignil::Reg B9 {kOffV9,  k8, "B9"};
inline const ignil::Reg B10{kOffV10, k8, "B10"};
inline const ignil::Reg B11{kOffV11, k8, "B11"};
inline const ignil::Reg B12{kOffV12, k8, "B12"};
inline const ignil::Reg B13{kOffV13, k8, "B13"};
inline const ignil::Reg B14{kOffV14, k8, "B14"};
inline const ignil::Reg B15{kOffV15, k8, "B15"};
inline const ignil::Reg B16{kOffV16, k8, "B16"};
inline const ignil::Reg B17{kOffV17, k8, "B17"};
inline const ignil::Reg B18{kOffV18, k8, "B18"};
inline const ignil::Reg B19{kOffV19, k8, "B19"};
inline const ignil::Reg B20{kOffV20, k8, "B20"};
inline const ignil::Reg B21{kOffV21, k8, "B21"};
inline const ignil::Reg B22{kOffV22, k8, "B22"};
inline const ignil::Reg B23{kOffV23, k8, "B23"};
inline const ignil::Reg B24{kOffV24, k8, "B24"};
inline const ignil::Reg B25{kOffV25, k8, "B25"};
inline const ignil::Reg B26{kOffV26, k8, "B26"};
inline const ignil::Reg B27{kOffV27, k8, "B27"};
inline const ignil::Reg B28{kOffV28, k8, "B28"};
inline const ignil::Reg B29{kOffV29, k8, "B29"};
inline const ignil::Reg B30{kOffV30, k8, "B30"};
inline const ignil::Reg B31{kOffV31, k8, "B31"};

// ---------------------------------------------------------------------------
// NZCV condition flags
// "VF" avoids collision with the V register prefix above.
// ---------------------------------------------------------------------------

inline const ignil::Reg NF{kOffNF, k8, "NF"}; ///< Negative
inline const ignil::Reg ZF{kOffZF, k8, "ZF"}; ///< Zero
inline const ignil::Reg CF{kOffCF, k8, "CF"}; ///< Carry
inline const ignil::Reg VF{kOffVF, k8, "VF"}; ///< Overflow

} // namespace dragon::arm
