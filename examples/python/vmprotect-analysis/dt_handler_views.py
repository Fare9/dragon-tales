#!/usr/bin/env python3
"""
dt_handler_views.py -- every view of VM_ENTRY, the dispatcher and all 256
VMProtect handler slots of build/x64/serial_check.vmp.exe, for Post.md.

Per handler:
  1. x86       capstone listing; instructions that dragon-tales' dead-store
               elimination proves dead (given VM_SP/VM_REGS/VIP/HTABLE live-out)
               are marked as junk.
  2. IGNIL     dragon-tales' IL, lifted as ONE trace block (lift_trace), then
               simplify() + dead_store_eliminate().  Instructions dragon cannot
               lift show up as UNDEF; the ones that SURVIVE DSE sit on the data
               path and are reported as "unlifted core ops".
  3. LLVM IR   dragon-tales LLVMLifter + mem2reg/instcombine/gvn/dce (reuses
               dt_catalog.devirt, which patches the tail jmp to a ret).
  4. symbolic  dragon-tales SymbolicExecutor: RBP/RSP/RSI/R11 are symbolized as
               VM_SP/VM_REGS/VIP/HTABLE (and pinned with assume() so memory
               addresses resolve), the VM stack / register file / bytecode /
               a popped pointer target are per-byte symbols.  Every register and
               memory slot the handler changed is reported as the Z3 term dragon
               built, then re-parsed into z3py and passed through z3.simplify().
  5. Binary Ninja IL, when analysis/vm/binja_il.json exists (binja_dump_il.py).

Also: VM_ENTRY symbolic (context layout + VIP derivation), the dispatcher
listing, a static linear decode of the bytecode stream at VIP using each
handler's recovered operand length, and an opcode summary.

Outputs: analysis/vm/post_views.json, Post-Handlers.md (repo root).
Run:     python3 dt_handler_views.py [--limit N]
"""

from __future__ import annotations

import sys, os, re, json, struct, collections, time
from typing import Optional, Sequence, TypedDict, cast

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = HERE  # outputs stay beside the scripts
# vmp_paths resolves everything environment-specific: where dragon.py lives,
# where the analysed binaries live, and where LLVM's tools are.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmp_paths import dragon_python_dir, require, target as _target

sys.path.insert(0, dragon_python_dir())
sys.path.insert(0, HERE)
import dragon
from dragon import x86
import z3
from capstone import Cs, CsInsn, CS_ARCH_X86, CS_MODE_64
from pe_report import PE
import dt_catalog  # LLVM-IR devirt + catalog naming

# ------------------------------------------------------------------ types --
# dragon-tales and capstone carry no annotations of their own, so the shapes
# this script passes around are declared here.  The dict-shaped returns are
# TypedDicts: calling e.g. SymView(...) builds a plain dict at runtime, so this
# is purely descriptive.

#: A dragon IGNIL register descriptor: (byte offset, width in bits, name).
Reg = tuple[int, int, str]

#: A symbolized memory region: (symbol base, start, size, display base, offset of index 0).
Region = tuple[str, int, int, str, int]

#: One handler's Binary Ninja IL dump: {"llil" | "mlil" | "hlil": lines}.
BinjaIL = dict[str, list[str]]


class IgnilViews(TypedDict):
    """What ignil_views() recovers from one handler's instruction sequence."""

    block: "dragon.IGNILBlock"
    keep: list["dragon.Graph"]  # graphs owning the lifted instructions; keep alive
    raw_ops: int  # IGNIL ops before simplify+DSE
    dse_ops: int  # IGNIL ops after
    dse_text: str
    live: set[int]  # instruction addresses that survived DSE
    unlifted: list[int]  # UNDEF defs sitting on the data path
    dropped: list[int]  # side effects that produced no IGNIL op at all
    skipped: list[int]  # addresses dragon could not decode


class RegEffect(TypedDict):
    """One register the handler changed, as a symbolic term."""

    reg: str  # native name, e.g. "RBP"
    role: str  # VM role if any, e.g. "VM_SP"
    dragon: str  # dragon's Z3 term after sym_rename()
    z3: str  # the same term re-parsed and z3.simplify()-ed
    delta: Optional[int]  # k, if the term is exactly <role> + k


class MemEffect(TypedDict):
    """One memory slot the handler changed."""

    addr: str  # e.g. "[VM_SP+0x8]"
    width: int  # in bits
    dragon: str
    z3: str


class SymView(TypedDict):
    """The result of running one block under the SymbolicExecutor."""

    ok: bool
    error: Optional[str]
    exit: Optional[int]  # dragon.SymExit
    complete: bool
    warnings: list[str]
    ops: int
    queries: int
    regs: list[RegEffect]
    mem: list[MemEffect]


class EntryViews(TypedDict):
    """Everything recovered about VM_ENTRY."""

    seq: list["CsInsn"]
    dse: str
    raw_ops: int
    dse_ops: int
    regs: dict[str, tuple[str, str]]  # register -> (dragon term, z3-simplified)
    saved: list[tuple[int, str]]  # (offset from NATIVE_SP, value)
    vip_concrete: str
    warnings: list[str]
    ok: bool


class HandlerView(TypedDict):
    """One fully analysed handler: every view plus the recovered name."""

    va: int
    slots: list[int]  # opcode slots dispatching here
    name: str  # recovered from the effect
    cat_name: str  # the older IR-shape catalog name
    exit: str
    exit_target: Optional[int]
    asm: list[str]
    live: list[int]
    unlifted: list[str]
    dropped: list[str]
    raw_ops: int
    dse_ops: int
    dse_text: str
    llvm: str
    sym: SymView
    effect: list[str]
    notes: list[str]
    vip_delta: Optional[int]  # operand bytes consumed
    vsp_delta: Optional[int]  # VM stack movement


class StreamItem(TypedDict):
    """One instruction of the statically decoded bytecode stream."""

    addr: int
    op: int
    handler: int
    name: str
    operand: Optional[str]


EXE = _target("serial_check.vmp.exe")
TABLE = 0x1400E93F0
FETCH, CHECK = 0x1400E5103, 0x1400E511C
DISPATCH = {FETCH, CHECK}
CHECK_SERIAL, ENTRY_STUB, VM_ENTRY = 0x140007300, 0x1400AB6E0, 0x1400E5000
VIP0 = 0x14018F959  # decoded from the entry key 0x59f91842 (see Post.md)

pe = PE(require(EXE))
BASE = pe.image_base
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
ARCH = dragon.Architecture.X86_64


def rd(va: int, n: int) -> bytes:
    o = pe.rva_to_off(va - BASE)
    return pe.data[o : o + n] if o is not None else b""


# ---------------------------------------------------------------- x86 walk --
def walk(va: int, maxins: int = 300) -> tuple[list[CsInsn], str, Optional[int]]:
    """
    Linear walk of a handler following unconditional jmps; stops (without
    including it) at the jmp back to the dispatcher, or after ret / an
    indirect jmp.  Returns (instructions, exit_kind, exit_target).

    This one will use Capstone to disassemble the instructions.

    :param va: The virtual address of the handler to walk.
    :param maxins: The maximum number of instructions to disassemble.
    :return: A tuple containing the list of disassembled instructions, the exit kind, and the exit target.
    """
    a = va
    seq: list[CsInsn] = []
    for _ in range(maxins):
        ins = next(md.disasm(rd(a, 16), a), None)
        if ins is None:
            return seq, "undecodable", a
        if ins.mnemonic == "jmp":
            try:
                t = int(ins.op_str, 16)
            except ValueError:
                seq.append(ins)
                return seq, "jmp-indirect", None
            if t in DISPATCH:
                return seq, ("fetch" if t == FETCH else "check"), t
            a = t
            continue
        seq.append(ins)
        if ins.mnemonic == "ret":
            return seq, "ret", None
        a += ins.size
    return seq, "overflow", a


def handler_slots() -> collections.OrderedDict[int, list[int]]:
    """
    :return: A dictionary mapping opcodes to their corresponding handler virtual addresses and names.
    """
    out: collections.OrderedDict[int, list[int]] = collections.OrderedDict()
    for op in range(256):
        h = (TABLE + struct.unpack("<i", rd(TABLE + op * 4, 4))[0]) & 0xFFFFFFFFFFFFFFFF
        out.setdefault(h, []).append(op)
    return out


# --------------------------------------------------------------- IGNIL views --
DIS = dragon.Disassembler(ARCH)
PREFIXES = set(range(0x40, 0x50)) | {
    0x66,
    0x67,
    0xF2,
    0xF3,
    0x2E,
    0x36,
    0x3E,
    0x26,
    0x64,
    0x65,
}


def canonical_bytes(ins: CsInsn) -> bytes:
    """
    VMProtect junk uses the undocumented `sal` encoding (shift group /6),
    which LLVM's decoder rejects.  /6 is an alias of /4 (`shl`) on every x86,
    so rewrite the ModRM reg field: same semantics, decodable.

    :param ins: The instruction to canonicalize.
    :return: The canonicalized bytes of the instruction.
    """
    b = bytearray(ins.bytes)
    if ins.mnemonic == "sal":
        k = 0
        while k < len(b) and b[k] in PREFIXES:
            k += 1
        if k + 1 < len(b) and b[k] in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
            b[k + 1] = (b[k + 1] & 0xC7) | 0x20
    return bytes(b)


def lift_trace(
    seq: Sequence[CsInsn],
) -> tuple[dragon.IGNILBlock, list[dragon.Graph], list[int]]:
    """
    One IGNIL block for the whole instruction sequence.  Returns the block,
    the graphs that own the instructions, and the addresses dragon could not
    decode (skipped).

    :param seq: Sequence of disassembled instructions to lift.
    :return: A tuple containing the lifted block, the list of graphs, and the list of skipped addresses.
    """
    keep: list[dragon.Graph] = []
    instrs: list[dragon.Instruction] = []
    skipped: list[int] = []
    for i in seq:
        g = DIS.disassemble(canonical_bytes(i), i.address)
        ins = g.get_instruction(i.address)
        if ins is None:
            skipped.append(i.address)
            continue
        keep.append(g)
        instrs.append(ins)
    blk = DIS.lift_trace(instrs)
    return blk, keep, skipped


# -------------------------------------------------------------- LLVM views --

# Next are registers that are alive according to LLVM optimization analysis
VM_LIVE: list[tuple[int, int]] = [
    (32, 64),
    (40, 64),
    (48, 64),
    (88, 64),
]  # RSP, RBP, RSI, R11
FLAGS = {"CF", "PF", "AF", "ZF", "SF", "OF", "DF"}


def ignil_views(seq: Sequence[CsInsn]) -> IgnilViews:
    """
    Lift the sequence of instructions to IGNIL, simplify and dead-store eliminate,
    and return a dictionary containing the block, the kept graphs, the number of raw and DSE operations,

    :param seq: Sequence of disassembled instructions to lift and analyze.
    :return: A dictionary containing the lifted block, the kept graphs,
        the number of raw and DSE operations,
        the DSE text, live addresses, unlifted addresses,
        dropped instructions, and skipped addresses.
    """
    # Lift to IGNIL the sequence of instructions
    blk, keep, skipped = lift_trace(seq)
    # string representation of the block
    raw_text = blk.to_string()
    # addresses of instructions that were in the original sequence but not lifted
    raw_addrs = set(int(m, 16) for m in re.findall(r"; @0x([0-9a-f]+)", raw_text))
    # Addresses of instructions that were UNDEF in the lifted block (not lifted by dragon)
    for m in re.finditer(
        r"([A-Z0-9]+):i(\d+) = UNDEF\(\)\s+; @0x([0-9a-f]+)", raw_text
    ):
        UNDEF_WIDTHS.setdefault(int(m.group(3), 16), set()).add(int(m.group(2)))
    # Simplify the block and perform dead store elimination, considering the VM registers as live-out
    simp, _ = blk.simplify()
    dse, removed = simp.dead_store_eliminate(ARCH, live_out=VM_LIVE)
    text = dse.to_string()
    live_addrs = set(int(m, 16) for m in re.findall(r"; @0x([0-9a-f]+)", text))
    unlifted: list[int] = []
    for l in text.splitlines():
        m = re.match(r"\s*([A-Z0-9]+):i\d+ = UNDEF\(\)\s+; @0x([0-9a-f]+)", l)
        if m and m.group(1) not in FLAGS:
            unlifted.append(int(m.group(2), 16))
    # instructions with side effects that produced NO IGNIL op at all
    dropped = [
        i.address
        for i in seq
        if i.address not in raw_addrs and i.mnemonic in CORE_DROPPED
    ]
    return IgnilViews(
        block=blk,
        keep=keep,
        raw_ops=blk.size,
        dse_ops=dse.size,
        dse_text=text,
        live=live_addrs,
        unlifted=sorted(set(unlifted)),
        dropped=dropped,
        skipped=skipped,
    )


# ---------------------------------------------------------------- LLVM view --
ROLE = {
    32: "RSP = VM_REGS",
    40: "RBP = VM_SP",
    48: "RSI = VIP",
    88: "R11 = HTABLE",
    0: "RAX",
    8: "RCX",
    16: "RDX",
    24: "RBX",
    56: "RDI",
    64: "R8",
    72: "R9",
    80: "R10",
    96: "R12",
    104: "R13",
    112: "R14",
    120: "R15",
    136: "CF",
    137: "PF",
    138: "AF",
    139: "ZF",
    140: "SF",
    141: "OF",
    142: "DF",
}


def llvm_view(va: int) -> tuple[str, str]:
    """
    Disassemble and lift to LLVM IR the function at the given virtual address,
    and annotate the IR with VM-role comments for readability.

    :param va: The virtual address of the function to be lifted.
    :return: A tuple containing the body of the lifted IR and the annotated IR.
    """
    ir = dt_catalog.devirt(va)
    body = dt_catalog.body_of(ir)
    out = []
    for l in body.splitlines():
        m = re.search(
            r"getelementptr i8, ptr %regs, i64 (\d+)$", l.split(";")[0].strip()
        )
        if m and int(m.group(1)) in ROLE:
            l += f"   ; {ROLE[int(m.group(1))]}"
        out.append(l)
    return body, "\n".join(out)


# ------------------------------------------------------------ symbolic view --
VSP, VREGS, VIP, P, HT = 0x20000, 0x10000, 0x30000, 0x40000, TABLE
REGIONS: list[Region] = (
    [  # (symbol base, start address, size, display base, offset of index 0)
        ("vstack", VSP - 0x100, 0x200, "VM_SP", -0x100),
        ("vreg", VREGS - 0x40, 0x140, "VM_REGS", -0x40),
        ("bc", VIP, 32, "VIP", 0),
        ("pm", P - 0x40, 0x80, "P", -0x40),
    ]
)
# Assumed bytecode bytes at VIP.  Handlers read the register-file index either
# as a byte or as a word, so bytes 0..1 are 0x0018 (index 0x18 either way).
OPERAND: list[int] = [0x18, 0x00] + [(0x10 + i) & 0xFF for i in range(2, 32)]
CORE_DROPPED = {
    "mul",
    "imul",
    "div",
    "idiv",
    "rdtsc",
    "cpuid",
}  # no IGNIL ops at all when unlifted
UNDEF_WIDTHS: dict[int, set[int]] = (
    {}
)  # instruction address -> widths of UNDEF defs there (for z3 re-parse)
SCRATCH: list[Reg] = [
    x86.RAX,
    x86.RCX,
    x86.RDX,
    x86.RBX,
    x86.RDI,
    x86.R8,
    x86.R9,
    x86.R10,
    x86.R12,
    x86.R13,
    x86.R14,
    x86.R15,
]
GPRS: list[Reg] = [
    x86.RAX,
    x86.RCX,
    x86.RDX,
    x86.RBX,
    x86.RSP,
    x86.RBP,
    x86.RSI,
    x86.RDI,
    x86.R8,
    x86.R9,
    x86.R10,
    x86.R11,
    x86.R12,
    x86.R13,
    x86.R14,
    x86.R15,
]
ROLE_OF: dict[str, str] = {
    "RBP": "VM_SP",
    "RSP": "VM_REGS",
    "RSI": "VIP",
    "R11": "HTABLE",
}
VM_ROLES: tuple[str, ...] = ("VM_SP", "VIP", "VM_REGS", "HTABLE")


def fmt_off(o: int) -> str:
    return f"+{o:#x}" if o >= 0 else f"-{-o:#x}"


def fresh_exec() -> dragon.SymbolicExecutor:
    """
    Generate a fresh symbolic executor with the VM roles symbolized and pinned to their concrete values.

    :return: A new instance of dragon.SymbolicExecutor with the VM roles symbolized and pinned.
    """
    # Generate the fresh symbolic executor with the VM roles symbolized and pinned to their concrete values.
    ex = dragon.SymbolicExecutor(stop_at_terminator=False, solver_timeout_ms=3000)
    for reg, name, val in (
        (x86.RBP, "VM_SP", VSP),
        (x86.RSP, "VM_REGS", VREGS),
        (x86.RSI, "VIP", VIP),
        (x86.R11, "HTABLE", HT),
    ):
        # Symbolize the register, this will provide a more meaningful name
        s = ex.symbolize_register(reg, name)
        ex.assume(s == val)
    # Symbolize the memory regions for the VM stack, register file, bytecode, and popped pointer
    for base, start, size, _, _ in REGIONS:
        ex.symbolize_memory(start, size, base)
    # Assume the bytecode bytes at VIP and the popped pointer value at VSP
    for i, b in enumerate(OPERAND):
        ex.assume(ex.read_memory(VIP + i, 8) == b)
    ex.assume(ex.read_memory(VSP, 64) == P)  # a popped pointer resolves to P
    # Symbolize the physical registers that dragon uses as scratch registers, so that their values are more meaningful in the symbolic execution
    for r in SCRATCH:
        ex.symbolize_register(r, r[2])
    ex.symbolize_register(x86.RIP, "RIP")
    return ex


WORD = {2: "w", 4: "d", 8: "q"}
FLAGSYM = {136: "CF", 137: "PF", 138: "AF", 139: "ZF", 140: "SF", 141: "OF", 142: "DF"}


def sym_rename(s: str) -> str:
    """dragon's per-byte symbols -> role-relative names, then collapse
    concat-of-consecutive-bytes into w/d/q words."""

    def byte_name(m: re.Match[str]) -> str:
        base, i = m.group(1), int(m.group(2))
        for b, _, _, disp, off0 in REGIONS:
            if b == base:
                return f"|b[{disp}{fmt_off(i + off0)}]|"
        return m.group(0)

    s = re.sub(r"\|(vstack|vreg|bc|pm)\[(\d+)\]\|", byte_name, s)

    def reg_name(
        m: re.Match[str],
    ) -> str:  # dragon's auto symbols for registers it read uninitialised
        i = int(m.group(1))
        if i in FLAGSYM:
            return f"|{FLAGSYM[i]}|"
        if 128 <= i < 136:
            return f"|b[RIP{fmt_off(i - 128)}]|"
        return m.group(0)

    s = re.sub(r"\|reg\[(\d+)\]\|", reg_name, s)

    def mem_name(
        m: re.Match[str],
    ) -> str:  # unmapped reads: place them relative to the VM roles
        a = int(m.group(1), 16)
        for _, start, size, disp, off0 in REGIONS:
            base = start - off0
            if start - 0x10000 <= a < start + 0x10000:
                return f"|b[{disp}{fmt_off(a - base)}]|"
        return m.group(0)

    s = re.sub(r"\|mem\[0x([0-9a-f]+)\]\|", mem_name, s)
    # Collapse runs of consecutive (descending, i.e. big-endian order inside a
    # concat) byte symbols into q/d/w words, greedily 8 > 4 > 2 bytes.
    run_pat = re.compile(r"(?:\|b\[[A-Z_]+[+-]0x[0-9a-f]+\]\|\s*){2,}")

    def collapse(m: re.Match[str]) -> str:
        items = re.findall(r"\|b\[([A-Z_]+)([+-]0x[0-9a-f]+)\]\|", m.group(0))
        toks = [(b, int(o, 16)) for b, o in items]
        out: list[str] = []
        i = 0
        while i < len(toks):
            done = False
            for n in (8, 4, 2):
                if i + n <= len(toks):
                    seg = toks[i : i + n]
                    if len({b for b, _ in seg}) == 1 and [o for _, o in seg] == list(
                        range(seg[0][1], seg[0][1] - n, -1)
                    ):
                        out.append(f"|{WORD[n]}[{seg[0][0]}{fmt_off(seg[-1][1])}]|")
                        i += n
                        done = True
                        break
            if not done:
                out.append(f"|b[{toks[i][0]}{fmt_off(toks[i][1])}]|")
                i += 1
        return " ".join(out) + (" " if m.group(0).endswith((" ", "\n")) else "")

    s = run_pat.sub(collapse, s)
    s = re.sub(r"\(concat\s+(\|[^|]+\|)\s*\)", r"\1", s)  # single-arg concat left over
    return s


BARE64 = ["VM_SP", "VM_REGS", "VIP", "HTABLE", "NATIVE_SP"] + [r[2] for r in GPRS]


def z3_parse(s: str) -> Optional[z3.ExprRef]:
    """Re-parse a dragon Z3 term string (after sym_rename) in z3py."""
    import itertools

    decls: list[str] = []
    for n in set(re.findall(r"\|([^|]+)\|", s)):
        w = (
            {"b": 8, "w": 16, "d": 32, "q": 64}.get(n[0])
            if re.match(r"[bwdq]\[", n)
            else 8
        )
        decls.append(f"(declare-const |{n}| (_ BitVec {w}))")
    for n in BARE64:
        if re.search(r"(?<![\w@|.\[])" + n + r"(?![\w\[])", s):
            decls.append(f"(declare-const {n} (_ BitVec 64))")
    # OPAQUE ops lower to Z3 *function applications* (`(cpuid.0 leaf subleaf)`),
    # not constants, so they need declare-fun or the re-parse fails. Arity and
    # widths follow IGNIL's OpaqueKind: cpuid takes leaf+subleaf, the clock
    # readings take nothing; every result is 32 bits wide.
    OPAQUE_ARITY = {"cpuid": 2, "rdtsc": 0, "rdtscp": 0}
    for kind, idx in sorted(set(re.findall(r"\((cpuid|rdtsc|rdtscp)\.(\d+)\s", s))):
        dom = " ".join(["(_ BitVec 32)"] * OPAQUE_ARITY[kind])
        decls.append(f"(declare-fun |{kind}.{idx}| ({dom}) (_ BitVec 32))")
    # ...and the application heads must be quoted to match the declaration.
    s = re.sub(r"\((cpuid|rdtsc|rdtscp)\.(\d+)", r"(|\1.\2|", s)

    undefs = sorted(set(re.findall(r"(?<![\w|])((?:undef|unmapped)@[\w.]+)", s)))
    body = f"(assert (= {s} {s}))"
    # candidate widths per undef symbol: what the IGNIL listing said was UNDEF at that address
    cands: list[list[int]] = []
    for u in undefs:
        m = re.match(r"undef@0x([0-9a-f]+)_", u)
        ws = (
            sorted(UNDEF_WIDTHS.get(int(m.group(1), 16), set()), reverse=True)
            if m
            else []
        )
        cands.append(
            [w for w in ws if w != 1] + [w for w in (64, 32, 16, 8, 1) if w not in ws]
        )
    tried = 0
    for combo in itertools.product(*cands) if undefs else [()]:
        tried += 1
        if tried > 400:
            break
        try:
            d = "".join(decls) + "".join(
                f"(declare-const {u} (_ BitVec {w}))" for u, w in zip(undefs, combo)
            )
            parsed = cast(z3.ExprRef, z3.parse_smt2_string(d + body)[0])
            return parsed.arg(0)
        except z3.Z3Exception:
            continue
    return None


def pretty(s: str) -> str:
    """z3py's printer: decimal literals -> hex, two's-complement negatives, one line."""

    def num(m: re.Match[str]) -> str:
        v = int(m.group(0))
        if (1 << 63) <= v < (1 << 64):
            return f"-{(1 << 64) - v:#x}"
        if 0xFFFF0000 <= v < (1 << 32):
            return f"-{(1 << 32) - v:#x}"
        return f"{v:#x}" if v >= 256 else m.group(0)

    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"(?<![\w.\[x])\d+(?![\w\]])", num, s)
    # two's-complement multipliers z3 uses for negation, per width
    s = re.sub(r"\b(?:255|0xffff|0xffffffff)\*(?=[bwd]\[)", "-", s)
    s = s.replace("-0x1*", "-").replace("+ -", "- ")
    return s


def z3_view(expr_str: str) -> tuple[str, str, Optional[z3.ExprRef]]:
    renamed = sym_rename(expr_str)
    e = z3_parse(renamed)
    if e is None:
        return renamed, "(z3 re-parse failed)", None
    se = z3.simplify(e)
    return renamed, pretty(str(se)), se


def const_delta(se: Optional[z3.ExprRef], base: str) -> Optional[int]:
    """If z3 expr `se` is base + k, return k (signed), else None."""
    if se is None or not z3.is_bv(se):
        return None
    try:
        d = z3.simplify(cast(z3.BitVecRef, se) - z3.BitVec(base, 64))
        if z3.is_bv_value(d):
            v = cast(z3.BitVecNumRef, d).as_long()
            return v - (1 << 64) if v >= (1 << 63) else v
    except z3.Z3Exception:
        pass
    return None


def sym_view(
    block: dragon.IGNILBlock, ex: Optional[dragon.SymbolicExecutor] = None
) -> SymView:
    ex = ex or fresh_exec()
    res = ex.run(block)
    out = SymView(
        ok=res.ok,
        error=res.error,
        exit=res.exit,
        complete=res.complete,
        warnings=[f"{w.address:#x}: {w.message}" for w in res.warnings],
        ops=res.ops_executed,
        queries=res.solver_queries,
        regs=[],
        mem=[],
    )
    for r in GPRS:
        name = ROLE_OF.get(r[2], r[2])
        raw = ex.read_register(r).to_string()
        if raw.strip() == name:
            continue
        renamed, simp, se = z3_view(raw)
        out["regs"].append(
            RegEffect(
                reg=r[2],
                role=name,
                dragon=renamed,
                z3=simp,
                delta=const_delta(se, name),
            )
        )
    for base, start, size, disp, off0 in REGIONS:
        changed: list[int] = []
        for i in range(size):
            if ex.read_memory(start + i, 8).to_string().strip() != f"|{base}[{i}]|":
                changed.append(i)
        groups: list[list[int]] = []
        for i in changed:
            if groups and groups[-1][1] == i:
                groups[-1][1] = i + 1
            else:
                groups.append([i, i + 1])
        for lo, hi in groups:
            i = lo
            while i < hi:
                n = hi - i
                w = 8 if n >= 8 else 4 if n >= 4 else 2 if n >= 2 else 1
                raw = ex.read_memory(start + i, w * 8).to_string()
                renamed, simp, _ = z3_view(raw)
                out["mem"].append(
                    MemEffect(
                        addr=f"[{disp}{fmt_off(i + off0)}]",
                        width=w * 8,
                        dragon=renamed,
                        z3=simp,
                    )
                )
                i += w
    return out


# ------------------------------------------------------ naming from the effect --
def parse_addr(a: str) -> tuple[Optional[str], Optional[int]]:
    m = re.fullmatch(r"\[([A-Z_]+)([+-]0x[0-9a-f]+)\]", a)
    return (m.group(1), int(m.group(2), 16)) if m else (None, None)


def classify_alu(z: str) -> Optional[str]:
    """Operator of a recovered ALU result expression (z3py infix, prettified)."""
    if re.search(r"~\([^()]*\|[^()]*\)", z) or (z.count("~") >= 2 and "&" in z):
        return "NOR"
    if re.search(r"~\([^()]*&[^()]*\)", z) or (z.count("~") >= 2 and "|" in z):
        return "NAND"
    if "LShR(" in z:
        return "SHR"
    if "<<" in z:
        return "SHL"
    if ">>" in z:
        return "SAR"
    if "UDiv(" in z or "URem(" in z:
        return "DIV"
    if re.search(r"(?<![\w\]])/(?!=)", z):
        return "IDIV"
    if re.search(r"- [qdwb]\[", z):
        return "SUB"
    if "*" in z:
        return "MUL"
    if " + " in z:
        return "ADD"
    if "&" in z:
        return "AND"
    if "|" in z:
        return "OR"
    if "^" in z:
        return "XOR"
    return None


def src_width(z: str, base: str) -> Optional[int]:
    """Number of source bytes of `base` referenced in a (possibly zero-extended) value."""
    m = re.fullmatch(r"([qdwb])\[" + base + r"[+-]0x[0-9a-f]+\]", z)
    if m:
        return {"q": 64, "d": 32, "w": 16, "b": 8}[m.group(1)]
    n = len(re.findall(r"\bb\[" + base + r"[+-]0x[0-9a-f]+\]", z))
    return n * 8 if n else None


def name_from_effect(
    sym: SymView,
    exit_kind: str,
    seq: Sequence[CsInsn],
    ig: IgnilViews,
    cat_name: str,
) -> tuple[str, list[str]]:
    """Primary naming: what the handler did to VM_SP / VIP / the register file /
    the VM stack, as recovered symbolically.  Falls back to the catalog name."""
    notes: list[str] = []
    regs = {r["role"]: r for r in sym["regs"]}
    vsp, vip = regs.get("VM_SP"), regs.get("VIP")
    # The `vsp["delta"] is None` case always returns below before dvsp is
    # read, so the extra guard only narrows the type to int here; no
    # reachable behaviour changes.
    dvsp: int = vsp["delta"] if vsp and vsp["delta"] is not None else 0
    mem = [m for m in sym["mem"] if not m["addr"].startswith("[VM_REGS-")]
    core = [i for i in seq if i.address in ig["unlifted"] or i.address in ig["dropped"]]
    core_addrs = set(ig["unlifted"]) | set(ig["dropped"])

    def w_of(i: CsInsn) -> int:
        return i.operands[0].size * 8 if i.operands else 0

    for i in core:
        kind = "no IGNIL ops" if i.address in ig["dropped"] else "UNDEF"
        notes.append(
            f"`{i.mnemonic} {i.op_str}`.strip() @ {i.address:#x} is not lifted by dragon ({kind})".replace(
                "`.strip()", "`"
            )
        )
    if not sym["ok"]:
        return cat_name, notes + ["symbolic run failed"]
    if exit_kind == "ret":
        return "VM_EXIT", notes + [
            "ends with ret: restores the native context and leaves the VM"
        ]
    if any(i.mnemonic == "call" for i in seq):
        return "VM_CALL", notes + [
            "calls the native function pointer at the top of the VM stack (operand byte = argument count)"
        ]
    # These used to be recognised by dragon failing to lift them. They now lift
    # to OPAQUE ops, so the test is on the live (post-DSE) instructions instead:
    # an instruction dragon models is still the handler's data path.
    for i in seq:
        if i.mnemonic in ("cpuid", "rdtsc", "rdtscp") and (
            i.address in ig["live"] or i.address in core_addrs
        ):
            return i.mnemonic.upper(), notes + [
                f"named from the assembly: `{i.mnemonic}` is the data path"
            ]
    if vsp and vsp["delta"] is None:
        z = vsp["z3"]
        if z == "q[VM_SP+0x0]":
            return "SET_VSP64", notes
        if "d[VM_SP+0x0]" in z:
            return "SET_VSP32", notes + [
                "mov ebp, [rbp]: the upper 32 bits are cleared"
            ]
        if "w[VM_SP+0x0]" in z:
            return "SET_VSP16", notes + ["mov bp, [rbp]: only the low 16 bits change"]
        return "SET_VSP?", notes + [f"VM_SP = {z}"]
    if vip and vip["delta"] is None:
        return "VM_JMP?", notes + [f"VIP = {vip['z3']}"]
    for i in core:
        if i.mnemonic in ("mul", "imul", "div", "idiv"):
            return f"{i.mnemonic.upper()}{w_of(i)}", notes + [
                f"named from the assembly: `{i.mnemonic}` is the data path"
            ]
    # ALU handlers: the live (post-DSE) arithmetic instruction is the data path;
    # the symbolic result expression is quoted as confirmation.
    live = [i for i in seq if i.address in ig["live"]]
    VMREGS = {"rbp", "rsi", "rsp", "r11", "bp", "si", "sp", "ebp", "esi", "esp", "r11d"}

    def dest(i: CsInsn) -> str:
        return i.op_str.split(",")[0].strip()

    alu = [
        i
        for i in live
        if i.mnemonic
        in (
            "add",
            "sub",
            "and",
            "or",
            "xor",
            "not",
            "shl",
            "shr",
            "sar",
            "shld",
            "shrd",
            "neg",
            "rol",
            "ror",
        )
        and dest(i) not in VMREGS
    ]
    if alu and any(parse_addr(m["addr"])[0] == "VM_SP" for m in mem):
        mn = [i.mnemonic for i in alu]
        last = alu[-1]  # the op whose EFLAGS get pushed
        w = w_of(last)
        m = last.mnemonic
        if "not" in mn and m == "and":
            nm = f"NOR{w}"
            how = "not a; not b; and a,b  ->  ~a & ~b = NOR(a,b)"
        elif "not" in mn and m == "or":
            nm = f"NAND{w}"
            how = "not a; not b; or a,b  ->  ~a | ~b = NAND(a,b)"
        elif m in ("shld", "shrd"):
            nm = f"{m.upper()}{w}"
            how = f"{m} dst, src, cl"
        elif m in ("shl", "shr", "sar"):
            nm = f"{m.upper()}{w}"
            how = f"{m} x, cl"
        elif m in ("add", "sub", "not", "neg", "and", "or", "xor", "rol", "ror"):
            nm = f"{m.upper()}{w}"
            how = f"{m} a, b"
        else:
            nm = f"{m.upper()}{w}"
            how = m
        res = [m for m in mem if parse_addr(m["addr"]) == ("VM_SP", dvsp + 8)] or [
            m for m in mem if parse_addr(m["addr"])[0] == "VM_SP" and m["width"] < 64
        ]
        conf = f"; result = {res[0]['z3'][:120]}" if res else ""
        return nm, notes + [
            f"data path `{' / '.join(f'{i.mnemonic} {i.op_str}' for i in alu)}` ({how}){conf}; EFLAGS pushed at [VM_SP{fmt_off(dvsp)}]"
        ]
    # register-file writes: POP into a virtual register
    for m in mem:
        b, o = parse_addr(m["addr"])
        if b == "VM_REGS":
            sw = src_width(m["z3"], "VM_SP")
            if sw:
                return f"POP_VR{sw}", notes + (
                    []
                    if sw == m["width"]
                    else [f"{sw}-bit value stored into a {m['width']}-bit slot"]
                )
            return f"POP_VR{m['width']}?", notes + [f"[VM_REGS+..] = {m['z3']}"]
    pw = [m for m in mem if parse_addr(m["addr"])[0] == "P"]
    if pw:
        m = pw[0]
        return f"STORE{m['width']}", notes + [
            "pops a pointer (P) and a value, writes the value at P"
        ]
    top = [m for m in mem if parse_addr(m["addr"]) == ("VM_SP", dvsp)]
    second = [m for m in mem if parse_addr(m["addr"]) == ("VM_SP", dvsp + 8)]
    if top:
        m = top[0]
        z = m["z3"]
        W = m["width"]
        for base, nm in (("VIP", "PUSH_IMM"), ("VM_REGS", "PUSH_VR"), ("P", "LOAD")):
            sw = src_width(z, base)
            if sw and (base in z):
                extra = (
                    []
                    if sw == W
                    else [f"{sw}-bit source zero-extended into a {W}-bit slot"]
                )
                return f"{nm}{sw}", notes + extra
        if "VM_SP" in z and not re.search(r"[qdwb]\[", z) and "undef" not in z:
            return f"PUSH_VSP{W}", notes + (
                [] if z == "VM_SP" else [f"pushed value = {z}"]
            )
        if second:
            op = classify_alu(second[0]["z3"])
            if op:
                return f"{op}{second[0]['width']}", notes + [
                    f"result = {second[0]['z3']}; flags pushed at [VM_SP{fmt_off(dvsp)}]"
                ]
        op = classify_alu(z)
        if op and ("VM_SP" in z):
            return f"{op}{W}", notes + [f"result = {z}"]
    if not mem and dvsp == 0 and not vip:
        return "NOP?", notes
    return cat_name, notes + ["kept catalog name (effect not matched by a rule)"]


# ----------------------------------------------------------------- VM_ENTRY --
def entry_views() -> EntryViews:
    seq: list[CsInsn] = []
    a = ENTRY_STUB
    for _ in range(400):
        ins = next(md.disasm(rd(a, 16), a), None)
        if ins is None or a == FETCH:
            break
        if ins.mnemonic == "jmp":
            a = int(ins.op_str, 16)
            continue
        if ins.mnemonic == "call":
            seq.append(ins)
            a = int(ins.op_str, 16)
            continue
        seq.append(ins)
        a += ins.size
    body = [i for i in seq if i.address >= VM_ENTRY]
    blk, keep, _ = lift_trace(body)
    simp, _ = blk.simplify()
    dse, _ = simp.dead_store_eliminate(ARCH, live_out=VM_LIVE)
    NSP = 0x50000
    KEY = 0x59F91842
    ex = dragon.SymbolicExecutor(stop_at_terminator=False, solver_timeout_ms=3000)
    s = ex.symbolize_register(x86.RSP, "NATIVE_SP")
    ex.assume(s == NSP)
    ex.symbolize_memory(NSP - 0x100, 0x120, "nstk")  # [NSP]=ret addr, [NSP+8]=key
    ex.assume(ex.read_memory(NSP + 8, 32) == KEY)
    for r in GPRS:
        if r[2] != "RSP":
            ex.symbolize_register(r, r[2])
    # the last instruction is `lea r11, [rip+0x42ed]`; RIP there = address of the fetch block
    ex.set_register(x86.RIP, FETCH)
    for m in re.finditer(
        r"([A-Z0-9]+):i(\d+) = UNDEF\(\)\s+; @0x([0-9a-f]+)", blk.to_string()
    ):
        UNDEF_WIDTHS.setdefault(int(m.group(3), 16), set()).add(int(m.group(2)))
    res = ex.run(blk)

    def view(raw: str) -> tuple[str, str, Optional[z3.ExprRef]]:
        s = re.sub(
            r"\|nstk\[(\d+)\]\|",
            lambda m: f"|b[NATIVE_SP{fmt_off(int(m.group(1)) - 0x100)}]|",
            raw,
        )
        s = sym_rename(s)
        e = z3_parse(s)
        return (
            s,
            (pretty(str(z3.simplify(e))) if e is not None else "(parse failed)"),
            e,
        )

    regs: dict[str, tuple[str, str]] = {}
    for r in (x86.RBP, x86.RSP, x86.RSI, x86.R11, x86.RBX, x86.RCX, x86.R8, x86.R9):
        regs[r[2]] = view(ex.read_register(r).to_string())[:2]
    _, _, e = view(ex.read_register(x86.RSI).to_string())
    vip_concrete = None
    if e is not None:
        subs = [
            (z3.BitVec("d[NATIVE_SP+0x8]", 32), z3.BitVecVal(KEY, 32)),
            (z3.BitVec("w[NATIVE_SP+0x8]", 16), z3.BitVecVal(KEY & 0xFFFF, 16)),
            (z3.BitVec("w[NATIVE_SP+0xa]", 16), z3.BitVecVal(KEY >> 16, 16)),
        ]
        for i in range(4):
            subs.append(
                (
                    z3.BitVec(f"b[NATIVE_SP{fmt_off(8 + i)}]", 8),
                    z3.BitVecVal((KEY >> (8 * i)) & 0xFF, 8),
                )
            )
        vip_concrete = z3.simplify(z3.substitute(e, *subs))
    saved: list[tuple[int, str]] = []
    for off in range(-0x100, 0x20, 8):
        raw = ex.read_memory(NSP + off, 64).to_string().strip()
        if "nstk[" in raw and raw.count("nstk[") == 8:
            continue  # untouched 8 original bytes
        saved.append((off, view(raw)[1]))
    return EntryViews(
        seq=seq,
        dse=dse.to_string(),
        raw_ops=blk.size,
        dse_ops=dse.size,
        regs=regs,
        saved=saved,
        vip_concrete=(
            f"{cast(z3.BitVecNumRef, vip_concrete).as_long():#x}"
            if vip_concrete is not None and z3.is_bv_value(vip_concrete)
            else str(vip_concrete)
        ),
        warnings=[f"{w.address:#x}: {w.message}" for w in res.warnings],
        ok=res.ok,
    )


def dispatcher_listing(start: int = FETCH, end: int = 0x1400E51F0) -> list[str]:
    out: list[str] = []
    for ins in md.disasm(rd(start, end - start), start):
        out.append(
            f"{ins.address:#x}: {ins.bytes.hex():<18} {ins.mnemonic} {ins.op_str}"
        )
    return out


# --------------------------------------------------------------- rendering --
def asm_lines(seq: Sequence[CsInsn], ig: IgnilViews) -> list[str]:
    out: list[str] = []
    for i in seq:
        if i.address in ig["dropped"]:
            tag = "   ; <-- NOT lifted by dragon (no IGNIL ops)"
        elif i.address in ig["unlifted"]:
            tag = "   ; <-- lifts to UNDEF"
        elif i.address in ig["live"]:
            tag = ""
        else:
            tag = "   ; junk (dead per DSE)"
        out.append(f"{i.address:#x}: {i.bytes.hex():<18} {i.mnemonic} {i.op_str}{tag}")
    return out


def effect_lines(sym: SymView) -> list[str]:
    lines: list[str] = []
    for r in sym["regs"]:
        if r["role"] in VM_ROLES:
            if r["delta"] is not None:
                if r["delta"] != 0:
                    lines.append(
                        f"{r['role']} {'+=' if r['delta'] >= 0 else '-='} {abs(r['delta']):#x}"
                    )
            else:
                lines.append(f"{r['role']} = {r['z3']}")
    for m in sym["mem"]:
        if m["addr"].startswith("[VM_REGS-"):
            continue  # native-stack scratch below the register file (pushfq/pop pairs)
        lines.append(f"{m['addr']}:{m['width']} = {m['z3']}")
    others = [r for r in sym["regs"] if r["role"] not in VM_ROLES]
    if others:
        lines.append(
            "scratch: " + ", ".join(f"{r['reg']}={r['z3'][:70]}" for r in others)
        )
    return lines


def render_handler(v: HandlerView, bn: Optional[BinjaIL]) -> str:
    slots = ", ".join(f"{o:#04x}" for o in v["slots"])
    L: list[str] = [
        f"## Handler `{v['va']:#x}` — `{v['name']}`  (opcode slot{'s' if len(v['slots'])>1 else ''} {slots})",
        "",
    ]
    L.append(
        f"- old catalog name: `{v['cat_name']}`; exit: **{v['exit']}**"
        + (f" -> `{v['exit_target']:#x}`" if v["exit_target"] else "")
        + f"; {len(v['asm'])} instructions, {len(v['live'])} live; IGNIL {v['raw_ops']} ops -> {v['dse_ops']} after simplify+DSE"
        + f"; operand bytes: {v['vip_delta']}; VM_SP delta: {v['vsp_delta']}"
    )
    if v["notes"]:
        L.append("- notes: " + "; ".join(v["notes"]))
    L.append("- **effect** (symbolic, after z3.simplify):")
    L.append("")
    L.append("```")
    L.extend(v["effect"] or ["(no VM-visible effect recovered)"])
    L.append("```")
    L.append("")
    L.append("<details><summary>x86 (capstone) — junk marked</summary>\n\n```asm")
    L.extend(v["asm"])
    L.append("```\n</details>\n")
    L.append(
        "<details><summary>IGNIL (dragon-tales lift_trace -> simplify -> DSE)</summary>\n\n```"
    )
    L.append(v["dse_text"].rstrip())
    L.append("```\n</details>\n")
    L.append(
        "<details><summary>LLVM IR (dragon-tales LLVMLifter, optimized)</summary>\n\n```llvm"
    )
    L.append(v["llvm"].rstrip() or "; (could not lift)")
    L.append("```\n</details>\n")
    L.append(
        "<details><summary>Symbolic (dragon-tales SymbolicExecutor -> Z3, then z3.simplify)</summary>\n\n```"
    )
    s = v["sym"]
    L.append(
        f"run ok={s['ok']} complete={s['complete']} ops={s['ops']} solver_queries={s['queries']}"
        + (f" error={s['error']}" if s["error"] else "")
    )
    for w in s["warnings"]:
        L.append(f"warning: {w}")
    for r in s["regs"]:
        L.append(f"{r['reg']} ({r['role']})")
        L.append(f"   dragon: {r['dragon']}")
        L.append(f"   z3    : {r['z3']}")
    for m in s["mem"]:
        L.append(f"{m['addr']}:{m['width']}")
        L.append(f"   dragon: {m['dragon']}")
        L.append(f"   z3    : {m['z3']}")
    L.append("```\n</details>\n")
    if bn:
        L.append(
            "<details><summary>Binary Ninja IL (LLIL / MLIL / HLIL)</summary>\n\n```"
        )
        for k in ("llil", "mlil", "hlil"):
            L.append(f"--- {k.upper()} ---")
            L.extend(bn.get(k, []))
        L.append("```\n</details>\n")
    else:
        L.append(
            "_Binary Ninja IL: not generated yet — run `analysis/vm/binja_dump_il.py` in the BN console, then re-run this script._\n"
        )
    return "\n".join(L)


# ------------------------------------------------------------------- main --
def main() -> None:
    limit: Optional[int] = None
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    bn_path = os.path.join(HERE, "binja_il.json")
    bn: dict[str, BinjaIL] = json.load(open(bn_path)) if os.path.exists(bn_path) else {}
    t0 = time.time()
    print("VM_ENTRY ...", flush=True)
    entry = entry_views()
    slots = handler_slots()
    views: collections.OrderedDict[int, HandlerView] = collections.OrderedDict()
    for n, (va, ops) in enumerate(slots.items()):
        if limit and n >= limit:
            break
        seq, exit_kind, exit_tgt = walk(va)
        ig = ignil_views(seq)
        body, llvm_annot = llvm_view(va)
        _, facts = dt_catalog.semantics(body)
        cat = dt_catalog.name_opcode(facts, body) if body.strip() else "(trivial)"
        try:
            sym = sym_view(ig["block"])
        except Exception as e:  # keep going; report
            sym = SymView(
                ok=False,
                error=str(e),
                exit=None,
                complete=False,
                warnings=[],
                ops=0,
                queries=0,
                regs=[],
                mem=[],
            )
        name, notes = name_from_effect(sym, exit_kind, seq, ig, cat)

        def delta(role: str) -> Optional[int]:
            for r in sym["regs"]:
                if r["role"] == role:
                    return r["delta"]
            return 0 if sym["ok"] else None

        v = HandlerView(
            va=va,
            slots=ops,
            name=name,
            cat_name=cat,
            exit=exit_kind,
            exit_target=exit_tgt,
            asm=asm_lines(seq, ig),
            live=sorted(ig["live"]),
            unlifted=[f"{a:#x}" for a in ig["unlifted"]],
            dropped=[f"{a:#x}" for a in ig["dropped"]],
            raw_ops=ig["raw_ops"],
            dse_ops=ig["dse_ops"],
            dse_text=ig["dse_text"],
            llvm=llvm_annot,
            sym=sym,
            effect=effect_lines(sym),
            notes=notes,
            vip_delta=delta("VIP"),
            vsp_delta=delta("VM_SP"),
        )
        views[va] = v
        print(
            f"[{n+1:3d}/{len(slots)}] {va:#x} slots={ops} {name:12s} vip_delta={v['vip_delta']} "
            f"vsp_delta={v['vsp_delta']} ({time.time()-t0:.0f}s)",
            flush=True,
        )

    # static linear decode of the bytecode stream
    by_op = {o: va for va, ops in slots.items() for o in ops}
    stream: list[StreamItem] = []
    a = VIP0
    for _ in range(120):
        op = rd(a, 1)[0]
        h = by_op[op]
        v = views.get(h)
        if v is None:
            break
        d = v["vip_delta"]
        if d is None or d < 0:
            stream.append(
                StreamItem(addr=a, op=op, handler=h, name=v["name"], operand=None)
            )
            break
        stream.append(
            StreamItem(
                addr=a, op=op, handler=h, name=v["name"], operand=rd(a + 1, d).hex()
            )
        )
        a += 1 + d
        if v["exit"] == "ret":
            break

    summary: collections.OrderedDict[str, list[int]] = collections.OrderedDict()
    for v in views.values():
        summary.setdefault(v["name"], []).append(v["va"])

    jv = {
        f"{va:#x}": {k: val for k, val in v.items() if k not in ("live",)}
        for va, v in views.items()
    }
    json.dump(
        dict(
            entry=dict(
                asm=[
                    f"{i.address:#x}: {i.bytes.hex():<18} {i.mnemonic} {i.op_str}"
                    for i in entry["seq"]
                ],
                dse=entry["dse"],
                raw_ops=entry["raw_ops"],
                dse_ops=entry["dse_ops"],
                regs=entry["regs"],
                saved=entry["saved"],
                vip_concrete=entry["vip_concrete"],
                warnings=entry["warnings"],
            ),
            dispatcher=dispatcher_listing(),
            handlers=jv,
            stream=stream,
            summary={k: [f"{x:#x}" for x in v] for k, v in summary.items()},
        ),
        open(os.path.join(HERE, "post_views.json"), "w"),
        indent=1,
        default=str,
    )

    out: list[str] = [
        "# Post appendix — every VMProtect handler, four views",
        "",
        "Generated by `analysis/vm/dt_handler_views.py` from `build/x64/serial_check.vmp.exe`. ",
        "Read `Post.md` first: it explains the VM, the register roles and how to read each view. ",
        "Symbol legend: `VM_SP` (RBP), `VM_REGS` (RSP), `VIP` (RSI), `HTABLE` (R11); ",
        "`q/d/w/b[VM_SP+0x8]` = 64/32/16/8-bit word of VM-stack memory at that offset on handler entry; ",
        "`b[VIP+0x0]` = the first operand byte (assumed 0x18 when it indexes the register file, so ",
        '`[VM_REGS+0x18]` means "the virtual register named by the operand byte"); `P` = the pointer ',
        "found at `[VM_SP+0x0]` on entry (for LOAD/STORE handlers). Flag words follow x86 EFLAGS bit layout.",
        "",
        "## Opcode summary",
        "",
        "| name | handlers | example |",
        "|---|---:|---|",
    ]
    for k, vs in sorted(summary.items(), key=lambda kv: -len(kv[1])):
        out.append(f"| `{k}` | {len(vs)} | `{vs[0]:#x}` |")
    out += [
        "",
        "## Static decode of the bytecode at VIP",
        "",
        f"Linear decode starting at VIP = `{VIP0:#x}` using each handler's recovered operand length (VIP delta).",
        "",
        "```",
    ]
    for s in stream:
        out.append(
            f"{s['addr']:#x}: op {s['op']:#04x} -> {s['handler']:#x} {s['name']:12s} operand {s['operand'] or '?'}"
        )
    out += ["```", "", "---", ""]
    for va, v in views.items():
        out.append(render_handler(v, bn.get(f"{va:#x}")))
        out.append("---\n")
    open(os.path.join(ROOT, "Post-Handlers.md"), "w", encoding="utf-8").write(
        "\n".join(out)
    )
    print(
        f"wrote Post-Handlers.md ({len(views)} handlers) and post_views.json in {time.time()-t0:.0f}s"
    )


if __name__ == "__main__":
    main()
