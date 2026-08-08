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
# mnemonics with a different encoding shape (INC/DEC/shifts/MOVZX/...) need
# their own audit and are intentionally not included here yet.
PRIORITY_MNEMONICS = [
    "MOV", "ADD", "SUB", "AND", "OR", "XOR", "CMP", "TEST", "ADC", "SBB",
]

# Every suffix ever used for this family, across every operand width.
FORM_SUFFIXES = ["rr", "ri", "ri8", "ri32", "rm", "mr", "mi", "mi8", "mi32"]
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

    if total_missing and not args.allow_missing:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
