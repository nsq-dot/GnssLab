# -*- coding: utf-8 -*-
"""Readers for the files the C++ solver produces, plus the RINEX header.

Two output formats are parsed, both documented in docs/data-format.md:

``<obs>_DUAL_IF.spp.out``
    Headerless. Seven whitespace-separated columns::

        year  doy  sod  timeSystem  X  Y  Z

    with XYZ in ECEF metres at three decimal places.

``<obs>_pos_vel.out``
    One header line (``sod X Y Z vE vN vU recClkDot``) then eight columns:
    second-of-day, ECEF X/Y/Z, local ENU velocity, and receiver clock drift
    expressed as a range rate (metres per second), all at six decimals.

The two files carry the same positions at different precision. Comparing them
shows a maximum |ΔXYZ| of 0.000499 m, which is exactly the half-step of the
3-decimal rounding in the ``.spp.out`` - not a discrepancy.
"""

from __future__ import annotations

import json
import os

import numpy as np

from ._strings import t

__all__ = [
    "load_spp_xyz",
    "load_pos_vel",
    "read_approx_position",
    "read_manifest",
    "output_paths",
]


def load_spp_xyz(fn: str):
    """Read ``..._spp.out`` -> ``(sod, X, Y, Z)`` as float64 arrays (metres)."""
    sod, x, y, z = [], [], [], []
    with open(fn, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            p = ln.split()
            if len(p) < 7:
                continue
            try:
                sod.append(float(p[2]))
                x.append(float(p[4]))
                y.append(float(p[5]))
                z.append(float(p[6]))
            except ValueError:
                continue
    return (np.asarray(sod), np.asarray(x), np.asarray(y), np.asarray(z))


def load_pos_vel(fn: str):
    """Read ``..._pos_vel.out`` -> ``(sod, X, Y, Z, vE, vN, vU, clkdot)``."""
    sod, x, y, z, ve, vn, vu, ck = [], [], [], [], [], [], [], []
    with open(fn, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            p = ln.split()
            try:
                sod.append(float(p[0]))
                x.append(float(p[1]))
                y.append(float(p[2]))
                z.append(float(p[3]))
                ve.append(float(p[4]))
                vn.append(float(p[5]))
                vu.append(float(p[6]))
                ck.append(float(p[7]))
            except (ValueError, IndexError):
                continue  # header line, or a short row
    return (np.asarray(sod), np.asarray(x), np.asarray(y), np.asarray(z),
            np.asarray(ve), np.asarray(vn), np.asarray(vu), np.asarray(ck))


def read_approx_position(rnx_path: str, lang: str = "en") -> np.ndarray:
    """Parse ``APPROX POSITION XYZ`` from a RINEX observation header.

    Returns a 3-element array of ECEF metres.

    This is the reference used for the position accuracy figures. It comes from
    the file's own header, so nothing is hardcoded - but note that it is only
    metre-to-decametre accurate, which the report calls out. See README.md.
    """
    with open(rnx_path, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            if "APPROX POSITION XYZ" in ln:
                return np.array([float(v) for v in ln[:42].split()])
            if "END OF HEADER" in ln:
                break
    raise RuntimeError(t(lang, "err_no_approx", path=rnx_path))


def read_manifest(path: str) -> dict | None:
    """Read the ``*_manifest.json`` written by ``apps/spp_if``.

    Returns ``None`` when the manifest is absent or unreadable, so callers can
    fall back to the filename convention.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def find_manifest(out_dir: str, obs_name: str | None = None) -> str | None:
    """Locate a manifest in `out_dir`, preferring one matching `obs_name`."""
    if not os.path.isdir(out_dir):
        return None
    candidates = [f for f in os.listdir(out_dir) if f.endswith("_manifest.json")]
    if not candidates:
        return None
    if obs_name:
        stem = os.path.basename(obs_name)
        for c in candidates:
            if c.startswith(stem):
                return os.path.join(out_dir, c)
    return os.path.join(out_dir, sorted(candidates)[0])


def output_paths(out_dir: str, rnx_name: str, mode: str = "DUAL_IF"):
    """Build the two output paths the solver writes for a given RINEX file.

    The solver names its output ``<obsFileName>_<mode>.spp.out``, keeping the
    original extension, so the result carries a doubled ``.rnx_`` in the middle.
    That looks odd but is deliberate and stable; the manifest is the reliable way
    to find the files, and this is the fallback.
    """
    base = os.path.join(out_dir, rnx_name)
    return {
        "spp": f"{base}_{mode}.spp.out",
        "vel": f"{base}_pos_vel.out",
        "manifest": f"{base}_manifest.json",
    }
