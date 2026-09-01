#!/usr/bin/env python3
"""Audit X86IGNILLifter's opcode coverage against LLVM's full X86 MC opcode
list, for the shared-encoding "arithmetic dyadic" instruction family
(ADD/SUB/AND/OR/XOR/CMP/TEST/ADC/SBB/MOV), across all their register/memory/
immediate operand-form variants (rr, ri, ri8, rm, mr, mi, mi8, ...).

This is the generalized version of the check that would have caught
ToDo/LifterGaps.md #1 automatically: the lifter's dispatch table only
recognized register-destination forms of MOV/ADD/SUB/CMP, so the
memory-destination forms (`mov [mem], imm`, RMW `add [mem], reg/imm`,
`cmp reg, [mem]`) silently produced zero IGNIL ops. Run this after adding a
new mnemonic form to make sure a sibling variant wasn't missed, and add
newly-covered mnemonics to PRIORITY_MNEMONICS as the lifter grows.

Usage:
    scripts/audit_x86_lifter_coverage.py [--bin PATH] [--allow-missing]

Exit status is 1 if any PRIORITY_MNEMONICS form is unhandled (0 with
--allow-missing, which just prints the report without failing - useful
while iterating on a new lifter form).
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LIFTER_SRC = REPO_ROOT / "lib" / "disassemblers" / "x86" / "X86IGNILLifter.cpp"
DEFAULT_BIN = REPO_ROOT / "build" / "bin" / "opcode_audit"

# The x86 "dyadic ALU op" mnemonic family: all of these share the exact same
# LLVM operand-form naming vocabulary (rr/ri/ri8/ri32/rm/mr/mi/mi8/mi32), so
# a single generic scan finds every variant of every mnemonic in the list.
# Grow this list as the lifter grows into more mnemonics of the same shape;
# mnemonics with a different encoding shape (INC/DEC/shifts/...) need their
# own audit and are intentionally not included here yet - except MOVZX/MOVSX,
# which get the exhaustive check below instead.
PRIORITY_MNEMONICS = [
    "MOV", "ADD", "SUB", "AND", "OR", "XOR", "CMP", "TEST", "ADC", "SBB",
]

# Every suffix ever used for this family, across every operand width. The
# bare "i8"/"i16"/"i32" are the accumulator forms (`xor eax, imm32`, opcode
# 0x35) - no ModRM byte, destination implicit. They were missing from this
# list, which is why ToDo/LifterGaps.md #5 went unreported for so long: not
# matching FORM_RE means not counted, so an unhandled form looks like no form
# at all rather than like a gap.
FORM_SUFFIXES = ["rr", "ri", "ri8", "ri32", "rm", "mr", "mi", "mi8", "mi32",
                 "i8", "i16", "i32"]
WIDTHS = ["8", "16", "32", "64"]

FORM_RE = re.compile(
    r"^(" + "|".join(PRIORITY_MNEMONICS) + r")("
    + "|".join(WIDTHS) + r")(" + "|".join(FORM_SUFFIXES) + r")$"
)


def get_llvm_opcodes(binary: Path) -> set[str]:
    if not binary.exists():
        sys.exit(
            f"error: {binary} not found - build it first "
            f"(`cmake --build build --target opcode_audit`)"
        )
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, check=True
    )
    return set(result.stdout.split())


def get_handled_mnemonics(src: Path) -> set[str]:
    text = src.read_text()
    return set(re.findall(r'\{"([A-Za-z0-9]+)",\s*&X86IGNILLifter::', text))


# MOVZX/MOVSX do not share the dyadic family's rr/ri/rm/mr vocabulary - their
# forms are rr/rm crossed with a source width (MOVZX32rm8), plus _NOREX
# variants - so FORM_RE cannot describe them and they get their own check.
# They are also not dispatched through kOpcodeFields: each spelling needs a
# width and an operand form, so the lifter keeps them in name -> width tables
# instead (see MovExtendForm). Coverage has to be exhaustive rather than
# best-effort, because an unlisted spelling does not fail loudly at the right
# place - it lifts to a bare UNDEF, which reads as "instruction not modelled"
# rather than "this instruction was forgotten". See ToDo/LifterGaps.md #4.
EXTEND_MNEMONICS = ("MOVZX", "MOVSX")


def get_extend_spellings(src: Path) -> set[str]:
    """Opcode names listed in the lifter's MOVZX/MOVSX width tables."""
    text = src.read_text()
    return {
        name
        for name in re.findall(r'\{"([A-Za-z0-9_]+)",\s*\d+\}', text)
        if name.startswith(EXTEND_MNEMONICS)
    }


def audit_extends(llvm_opcodes: set[str], src: Path) -> list[str]:
    """Every MOVZX/MOVSX opcode LLVM decodes but the lifter does not list."""
    listed = get_extend_spellings(src)
    known = {n for n in llvm_opcodes if n.startswith(EXTEND_MNEMONICS)}
    return sorted(known - listed)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, default=DEFAULT_BIN,
                        help="path to the opcode_audit tool")
    parser.add_argument("--allow-missing", action="store_true",
                        help="print the report but always exit 0")
    args = parser.parse_args()

    llvm_opcodes = get_llvm_opcodes(args.bin)
    handled = get_handled_mnemonics(LIFTER_SRC)

    # mnemonic -> {form: covered?}, only for forms LLVM actually has.
    matrix: dict[str, dict[str, bool]] = {m: {} for m in PRIORITY_MNEMONICS}
    for name in llvm_opcodes:
        m = FORM_RE.match(name)
        if not m:
            continue
        mnemonic, width, suffix = m.groups()
        matrix[mnemonic][f"{width}{suffix}"] = name in handled

    total_forms = 0
    total_missing = 0
    missing_list: list[str] = []

    print(f"{'mnemonic':<8} {'covered/total':<15} forms missing")
    print("-" * 70)
    for mnemonic in PRIORITY_MNEMONICS:
        forms = matrix[mnemonic]
        if not forms:
            continue
        covered = sum(1 for ok in forms.values() if ok)
        total = len(forms)
        missing = sorted(f for f, ok in forms.items() if not ok)
        total_forms += total
        total_missing += len(missing)
        missing_list.extend(f"{mnemonic}{f}" for f in missing)

        marker = "OK" if not missing else "GAP"
        print(f"{mnemonic:<8} {covered}/{total:<13} {', '.join(missing):<30} [{marker}]")

    print("-" * 70)
    print(f"Total: {total_forms - total_missing}/{total_forms} forms covered "
          f"across {len(PRIORITY_MNEMONICS)} priority mnemonics")

    if total_missing:
        print(f"\nMissing opcode dispatch entries ({total_missing}):")
        for name in missing_list:
            print(f"  {name}")

    # MOVZX/MOVSX, checked exhaustively rather than by operand-form matrix.
    extend_missing = audit_extends(llvm_opcodes, LIFTER_SRC)
    extend_total = len([n for n in llvm_opcodes
                        if n.startswith(EXTEND_MNEMONICS)])
    print()
    print(f"MOVZX/MOVSX: {extend_total - len(extend_missing)}/{extend_total} "
          f"spellings listed "
          f"[{'OK' if not extend_missing else 'GAP'}]")
    if extend_missing:
        print(f"\nMissing MOVZX/MOVSX width-table entries "
              f"({len(extend_missing)}):")
        for name in extend_missing:
            print(f"  {name}")

    if (total_missing or extend_missing) and not args.allow_missing:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
