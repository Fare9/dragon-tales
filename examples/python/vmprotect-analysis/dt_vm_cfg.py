#!/usr/bin/env python3
"""Recover the *virtual program's* control-flow graph, not just one path.

`dt_vm_trace.py` gives a linear stream of VM instructions -- one path through
the bytecode for one input. `dt_recompile.py` consumes that stream and emits
straight-line code. Neither recovers control flow: the loop is unrolled and the
branches were decided before the recompiler ever ran.

This script takes the step in between. The idea is small enough to state in
three lines:

  1. In the virtual program, **VIP is the address**. Two VM instructions with
     the same VIP are the same instruction, however far apart in the trace.
  2. Most VM instructions advance VIP by a fixed amount -- one opcode byte plus
     that opcode's operand bytes. Anything else is a control transfer.
  3. A transfer to a VIP already recovered is a **back edge**: a loop, and a
     place exploration can stop instead of going round again.

Point 3 is the payoff. It turns "trace until it stops" into "explore until
nothing new is reachable", and it is why the loop comes out as a loop instead of
as 220 repeated instructions.

Paths are discovered rather than assumed. `dt_vm_symexec.run_path` raises `Fork`
whenever a branch has more than one feasible successor, handing back both the
choice it made and the alternatives it did not take; `collect_paths` re-runs
with each of those until no new branch appears. That is the "guided" part: the
engine proposes the branches, we only choose among them.

What is *not* here: a VM jump whose target is a symbolic *expression* rather
than a choice between already-resolved addresses. Recovering those means
`SymbolicExecutor.enumerate()` on the VIP term -- which slots into the same
worklist, just with more successors to schedule.

Run: python3 dt_vm_cfg.py [--serial AB] [--max-vm 800] [--dot cfg.dot]
"""

from __future__ import annotations

import argparse
import collections
import os
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dt_vm_symexec as S
from dragon import x86
from dt_vm_trace import VMStep


@dataclass
class VMBlock:
    """A straight-line run of VM instructions, named by the VIP it starts at."""

    vip: int
    steps: list[VMStep] = field(default_factory=list)
    #: VIPs control can reach from the end of this block.
    successors: set[int] = field(default_factory=set)

    def __len__(self) -> int:
        return len(self.steps)


class _Enough(Exception):
    """Raised from the probe to stop a run that has produced enough steps."""


def collect_paths(serial: str, max_vm: int, max_paths: int = 8
                  ) -> tuple[list[list[VMStep]], list[tuple[int, list[str]]]]:
    """Run the VM down every branch we can reach; one trace per path.

    Re-running from the start for each alternative (rather than snapshotting at
    the fork) keeps this readable at the cost of redoing the prefix. A real
    explorer would save it -- `SymbolicExecutor.snapshot()` / `restore()` exist
    for exactly that.

    :return: (traces, branch points as (native pc, candidate addresses)).
    """
    queue: list[list[int]] = [[]]
    traces: list[list[VMStep]] = []
    forks: list[tuple[int, list[str]]] = []
    seen_forks: set[int] = set()

    while queue and len(traces) < max_paths:
        choices = queue.pop(0)
        steps: list[VMStep] = []

        def probe(pc: int, ex, n, tr, kind) -> None:
            if pc not in S.HANDLERS:
                return
            vip = ex.evaluate(ex.read_register(x86.RSI))
            if vip is None:
                return  # VIP went symbolic: a jump we cannot follow concretely
            steps.append((vip, pc, S.NAMES.get(pc, "?"),
                          S.rd(vip - 1, 1)[0] if vip else None))
            if len(steps) >= max_vm:
                raise _Enough()

        try:
            S.run_path(len(serial), choices, concrete=serial,
                       probe=probe, max_steps=6_000_000)
        except S.Fork as f:
            # The engine found a branch it will not decide for us. Take the
            # side it was on, and queue the others for their own run.
            this, alts, pc, targets = f.args[0]
            if pc not in seen_forks:
                seen_forks.add(pc)
                forks.append((pc, targets))
            queue.insert(0, this)
            queue.extend(alts)
            continue
        except _Enough:
            pass
        except Exception as e:   # a path that dies is still a path we explored
            print(f"  path {choices}: stopped ({type(e).__name__}: {e})")
        if steps:
            traces.append(steps)
    return traces, forks


def infer_lengths(traces: list[list[VMStep]]) -> dict[int, int]:
    """Handler address -> how many bytes its VM instruction occupies.

    Recovered from the traces themselves rather than from a handler catalogue:
    when a handler runs and control falls through, VIP advances by exactly this
    handler's length. Jumps produce a delta too, but they are the minority, so
    the *most common* forward delta per handler is its length.

    This is the one measurement everything else rests on -- get it wrong and
    ordinary fallthrough looks like a branch.
    """
    deltas: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
    for trace in traces:
        for (vip, handler, _, _), (next_vip, _, _, _) in zip(trace, trace[1:]):
            step = next_vip - vip
            if 0 < step <= 16:   # plausible length; anything else is a jump
                deltas[handler][step] += 1
    return {h: c.most_common(1)[0][0] for h, c in deltas.items()}


#: Opcodes that end a block whatever VIP does next. VM_EXIT leaves the VM
#: entirely (the native context is restored and the function returns), so the
#: instruction after it belongs to a different block even when it happens to
#: sit at the next address -- which on this build it does, because VMProtect
#: re-enters the VM several times before the real work starts.
TERMINATORS = {"VM_EXIT"}


def transfers(trace: list[VMStep], lengths: dict[int, int]) -> set[int]:
    """Indices in `trace` whose VM instruction ended a block.

    Two reasons a block ends: control did not fall through to the next
    instruction, or the instruction is a terminator by nature. Missing the
    second kind is invisible in the structural check -- every instruction is
    still in one block and every transition is still an edge -- but it puts a
    `ret` in the middle of a basic block, which is where it shows up.
    """
    out = set()
    for i, ((vip, handler, name, _), (nxt, _, _, _)) in enumerate(zip(trace, trace[1:])):
        if name.rstrip("?") in TERMINATORS or nxt != vip + lengths.get(handler, 1):
            out.add(i)
    return out


def explore(traces: list[list[VMStep]]
            ) -> tuple[dict[int, VMBlock], set[tuple[int, int]]]:
    """Merge every discovered path into one CFG keyed by VIP.

    Each trace is cut at its control transfers, and blocks from different paths
    that start at the same VIP are *the same block* -- which is what makes this
    a graph rather than a bundle of traces, and why reaching an already-known
    target costs nothing.

    :return: (blocks by entry VIP, back edges as (from_vip, to_vip)).
    """
    lengths = infer_lengths(traces)
    blocks: dict[int, VMBlock] = {}
    back_edges: set[tuple[int, int]] = set()

    for trace in traces:
        ends = transfers(trace, lengths)
        # A back edge is an edge to a block this path has *already been through*
        # -- the virtual program returned somewhere it had been.
        #
        # Not address order. VMProtect does not lay the bytecode out
        # monotonically: on this build execution starts at the highest block
        # address, so "target <= source" calls almost every ordinary forward
        # jump a loop. Order of arrival is the thing that actually means
        # "looped", and it needs no dominator tree.
        visited_here: set[int] = set()
        start = 0
        for i in range(len(trace)):
            last = i + 1 >= len(trace)
            if i not in ends and not last:
                continue
            vip = trace[start][0]
            visited_here.add(vip)
            if vip not in blocks:                      # already recovered?
                blocks[vip] = VMBlock(vip, list(trace[start:i + 1]))
            if not last:
                nxt = trace[i + 1][0]
                blocks[vip].successors.add(nxt)
                if nxt in visited_here:
                    back_edges.add((vip, nxt))
            start = i + 1

    return blocks, back_edges


def to_dot(blocks: dict[int, VMBlock], back_edges: set[tuple[int, int]]) -> str:
    """Graphviz source, so the recovered shape can actually be looked at."""
    out = ["digraph vm {", '  node [shape=box fontname="monospace" fontsize=9];']
    for vip, b in sorted(blocks.items()):
        body = "\\l".join(name for _, _, name, _ in b.steps[:8])
        more = "\\l..." if len(b) > 8 else ""
        out.append(f'  "{vip:#x}" [label="{vip:#x}  ({len(b)} instr)\\l\\l{body}{more}\\l"];')
    for b in blocks.values():
        for s in b.successors:
            style = ' [color=red style=bold label="back"]' if (b.vip, s) in back_edges else ""
            out.append(f'  "{b.vip:#x}" -> "{s:#x}"{style};')
    out.append("}")
    return "\n".join(out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--serial", default="AB", help="concrete input to explore from")
    ap.add_argument("--max-vm", type=int, default=800, help="VM instructions per path")
    ap.add_argument("--max-paths", type=int, default=8)
    ap.add_argument("--dot", help="write Graphviz source here")
    a = ap.parse_args()

    if not S.NAMES:
        print("note: opcode names come from post_views.json -- run "
              "dt_handler_views.py first to see them instead of '?'.\n")
    print(f"exploring the VM on {a.serial!r} (up to {a.max_paths} paths) ...")
    traces, forks = collect_paths(a.serial, a.max_vm, a.max_paths)
    total = sum(len(t) for t in traces)
    print(f"  {len(traces)} path(s), {total} VM instructions, {len(forks)} branch point(s)")
    for pc, targets in forks:
        # 0x1400e511b is the `push rcx; ret` the dispatcher jumps through:
        # the VM's indirect dispatch *is* its branch.
        where = " (the dispatcher's ret)" if pc == S.FETCH + 0x18 else ""
        print(f"    fork at {pc:#x} -> {', '.join(targets)}{where}")
    if not traces:
        print("  nothing explored; is the target binary reachable? (VMP_TARGET_DIR)")
        return

    blocks, back_edges = explore(traces)
    print(f"\nbasic blocks: {len(blocks)}")
    print(f"back edges  : {len(back_edges)}")
    for src, dst in sorted(back_edges):
        body = sum(len(b) for v, b in blocks.items() if dst <= v <= src)
        print(f"  {src:#x} -> {dst:#x}   loop body ~{body} VM instructions")

    print("\nlargest blocks:")
    for b in sorted(blocks.values(), key=len, reverse=True)[:5]:
        names = ", ".join(n for _, _, n, _ in b.steps[:6])
        print(f"  {b.vip:#x}  {len(b):4d} instr  {names}...")

    recovered = sum(len(b) for b in blocks.values())
    if total > recovered:
        print(f"\n{total - recovered} of {total} executed instructions were re-runs of "
              f"blocks already recovered ({(total - recovered) * 100 // total}%); "
              f"the CFG holds {recovered} distinct ones.")

    problems = validate(traces, blocks)
    if problems:
        print(f"\nCFG does NOT cover the traces -- {len(problems)} problem(s):")
        for m in problems[:10]:
            print(f"  {m}")
    else:
        print("\nCFG check: every executed instruction is in exactly one block, "
              "and every transition is an edge.")

    if a.dot:
        open(a.dot, "w").write(to_dot(blocks, back_edges))
        print(f"\nwrote {a.dot}   (render: dot -Tpng {a.dot} -o cfg.png)")



# ---------------------------------------------------------------------------
# Is the recovered CFG actually right?
# ---------------------------------------------------------------------------

def validate(traces: list[list[VMStep]],
             blocks: dict[int, VMBlock]) -> list[str]:
    """Check the recovered CFG against the traces it was built from.

    A CFG is only useful if walking it reproduces what actually ran, so this
    asserts exactly that, structurally and without interpreting anything:

      * every executed instruction lies in exactly one block, at the offset the
        block says -- catches a block boundary in the wrong place, and catches
        the same instruction ending up in two blocks;
      * every consecutive pair in a trace is either a step *inside* a block or
        an *edge* of the graph -- catches a missing edge, which is the failure
        that would silently make a devirtualized program skip code.

    Cheap, and it fails loudly on the two mistakes that matter. What it cannot
    tell you is whether a block you never executed is reachable -- for that you
    need the symbolic successor enumeration this module does not do yet.

    :return: human-readable problems; empty means the CFG covers the traces.
    """
    problems: list[str] = []

    # Where does each VIP live? (block entry VIP, index within that block)
    home: dict[int, tuple[int, int]] = {}
    for b in blocks.values():
        for i, (vip, _, _, _) in enumerate(b.steps):
            if vip in home:
                other = home[vip][0]
                problems.append(f"{vip:#x} appears in both {other:#x} and {b.vip:#x}")
            home[vip] = (b.vip, i)

    edges = {(b.vip, s) for b in blocks.values() for s in b.successors}

    for t, trace in enumerate(traces):
        for (vip, _, _, _), (nxt, _, _, _) in zip(trace, trace[1:]):
            if vip not in home:
                problems.append(f"path {t}: {vip:#x} is in no block")
                continue
            blk, idx = home[vip]
            body = blocks[blk].steps
            if idx + 1 < len(body):
                # Mid-block: the next instruction must be the next one here.
                if body[idx + 1][0] != nxt:
                    problems.append(
                        f"path {t}: inside {blk:#x}, {vip:#x} -> {nxt:#x} "
                        f"but the block continues to {body[idx + 1][0]:#x}")
            elif (blk, nxt) not in edges:
                # End of block: there must be an edge for where it went.
                problems.append(f"path {t}: no edge {blk:#x} -> {nxt:#x}")
    return problems


if __name__ == "__main__":
    main()
