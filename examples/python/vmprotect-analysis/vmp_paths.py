#!/usr/bin/env python3
"""Locating things the VMProtect example scripts need.

dragon-tales ships the scripts but not the binaries they analyse, and not an
LLVM install.  Everything environment-specific is resolved here so the analysis
scripts stay readable.

Environment variables
---------------------
``DRAGON_TALES_PY``
    Directory holding ``dragon.py``.  Defaults to this checkout's ``python/``,
    so a fresh clone needs nothing set.
``VMP_TARGET_DIR``
    Directory holding the binaries under analysis.  Defaults to this directory.
``LLVM_BINDIR``
    Directory holding ``opt`` / ``lli``.  Defaults to whatever is on ``PATH``.
"""

from __future__ import annotations

import os
import shutil
from typing import Optional

HERE = os.path.dirname(os.path.abspath(__file__))

#: This checkout's own ``python/`` directory (examples/python/<this>/../../..).
REPO_PYTHON = os.path.abspath(os.path.join(HERE, "..", "..", "..", "python"))

#: Where the companion binaries can be fetched from, quoted in error messages.
COMPANION_REPO = "https://github.com/Fare9/Dragons-vs-VMs"


def dragon_python_dir() -> str:
    """Directory to put on ``sys.path`` so ``import dragon`` works."""
    return os.environ.get("DRAGON_TALES_PY", REPO_PYTHON)


def target(name: str) -> str:
    """Absolute path of an analysed binary, e.g. ``serial_check.vmp.exe``.

    The binaries are not redistributed with dragon-tales.  Returns the expected
    path whether or not it exists, so a script can be imported (for its helper
    functions, or by a type checker) without the binary present; call
    :func:`require` when you are about to read it.
    """
    return os.path.join(os.environ.get("VMP_TARGET_DIR", HERE), name)


def require(path: str) -> str:
    """`path`, or a FileNotFoundError that says how to obtain it."""
    if os.path.exists(path):
        return path
    raise FileNotFoundError(
        f"{path} not found.\n"
        f"The VMProtect samples are not shipped with dragon-tales; fetch them "
        f"from {COMPANION_REPO} and either drop them beside these scripts or "
        f"set VMP_TARGET_DIR to the directory holding them."
    )


def llvm_tool(name: str) -> Optional[str]:
    """Path to an LLVM tool (``opt``, ``lli``), or None if it is not installed.

    Callers treat a missing tool as "skip the optimize/run step" rather than as
    an error, so the analysis still produces its raw IR on a machine without
    LLVM's command-line tools.
    """
    bindir = os.environ.get("LLVM_BINDIR")
    if bindir:
        for candidate in (os.path.join(bindir, name), os.path.join(bindir, name + ".exe")):
            if os.path.exists(candidate):
                return candidate
        return None
    return shutil.which(name)
