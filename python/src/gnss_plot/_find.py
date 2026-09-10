# -*- coding: utf-8 -*-
"""Locate the build toolchain and the compiled solver executables.

Neither CMake nor Ninja is guaranteed to be on PATH: a JetBrains IDE bundles
both internally and never exposes them, which is the common case on a machine
where CLion installed the toolchain. Searching a few known locations is more
useful than printing "cmake not found" and stopping.
"""

from __future__ import annotations

import glob
import os
import shutil

__all__ = ["find_cmake", "find_ninja", "find_binary", "project_root", "bin_dir"]

_EXE = ".exe" if os.name == "nt" else ""


def project_root(start: str | None = None) -> str:
    """Return the repository root - the nearest ancestor holding CMakeLists.txt."""
    here = os.path.abspath(start or os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
    for _ in range(6):
        if os.path.isfile(os.path.join(here, "CMakeLists.txt")):
            return here
        parent = os.path.dirname(here)
        if parent == here:
            break
        here = parent
    # Fall back to the directory two levels above this package (python/src/gnss_plot).
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def find_cmake() -> str | None:
    """Locate a CMake executable, or return None."""
    env = os.environ.get("CMAKE_EXE")
    if env and os.path.isfile(env):
        return env

    found = shutil.which("cmake")
    if found:
        return found

    patterns = [
        # JetBrains IDEs bundle CMake under the IDE installation.
        r"D:\CLion */bin/cmake/win/x64/bin/cmake.exe",
        r"C:\Program Files\JetBrains\*\bin\cmake\win\x64\bin\cmake.exe",
        r"C:\Program Files\CMake\bin\cmake.exe",
        r"C:\Program Files (x86)\CMake\bin\cmake.exe",
        # Common manual installs.
        r"C:\ProgramData\chocolatey\bin\cmake.exe",
        os.path.expanduser(r"~\scoop\shims\cmake.exe"),
        "/usr/local/bin/cmake",
        "/opt/homebrew/bin/cmake",
    ]
    for pat in patterns:
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]  # newest IDE version sorts last
    return None


def find_ninja() -> str | None:
    """Locate a Ninja executable, or return None."""
    env = os.environ.get("NINJA_EXE")
    if env and os.path.isfile(env):
        return env

    found = shutil.which("ninja")
    if found:
        return found

    patterns = [
        r"D:\CLion */bin/ninja/win/x64/ninja.exe",
        r"C:\Program Files\JetBrains\*\bin\ninja\win\x64\ninja.exe",
        r"C:\ProgramData\chocolatey\bin\ninja.exe",
        os.path.expanduser(r"~\scoop\shims\ninja.exe"),
        "/usr/local/bin/ninja",
        "/opt/homebrew/bin/ninja",
    ]
    for pat in patterns:
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]
    return None


def bin_dir(root: str | None = None) -> str:
    """The directory the build puts executables in."""
    return os.path.join(root or project_root(), "build", "bin")


def find_binary(name: str, root: str | None = None) -> str | None:
    """Locate a compiled executable by target name.

    Searched in order: ``$GNSS_BIN_DIR``, then ``build/bin`` for the common
    build presets, then PATH. Returns None if not found, so the caller can
    suggest running ``gnss build``.
    """
    root = root or project_root()
    exe = name if name.endswith(_EXE) else f"{name}{_EXE}"

    env = os.environ.get("GNSS_BIN_DIR")
    if env:
        cand = os.path.join(env, exe)
        if os.path.isfile(cand):
            return cand

    for sub in ("build/bin", "build-debug/bin", "cmake-build-debug/bin", "cmake-build-debug", "bin"):
        cand = os.path.join(root, sub, exe)
        if os.path.isfile(cand):
            return cand

    return shutil.which(name)
