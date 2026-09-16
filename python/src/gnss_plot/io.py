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

The chapter-7 cycle-slip detector output has its own three readers further down
(``load_gf_summary``, ``load_gf_detector``, ``load_slip_manifest``); the formats
they parse are described next to them.
"""

from __future__ import annotations

import csv
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
    "load_gf_summary",
    "load_gf_detector",
    "load_slip_manifest",
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


# ---------------------------------------------------------------------------
# Cycle-slip detector output (chapter 7)
# ---------------------------------------------------------------------------
# Three formats, all written by apps/cs_detect_gf.cpp and apps/cs_detect_mw.cpp:
#
# ``summary.<mode>.csv``
#     Comma-separated, with a header line naming the columns, one row per
#     satellite, a ``TOTAL`` row, and a ``# key,value`` metadata footer.
#
# ``<sat>.gf.diff`` / ``<sat>.gf.poly`` / ``<sat>.mw``
#     Space-separated, with the COLUMN SPEC in the leading ``#`` comment lines,
#     followed by one row per epoch. The last column is the detector's
#     judgement: a status token for the GF detectors (``OK``/``SLIP``/``INIT``/
#     ``GAP``/``WARMUP``) or a 0/1 flag for MW.
#
# ``<obs>.slips.csv``
#     The ground truth written by scripts/inject_cycle_slips.py.

#: Columns of a detector file that hold text rather than numbers.
_TEXT_COLUMNS = ("sat", "timeSystem")


def load_gf_summary(fn: str):
    """Read ``summary.<mode>.csv``.

    Returns ``(sat, cols, meta, total)``:

    * ``sat``   - (n,) array of satellite ids in file order, ``TOTAL`` excluded
    * ``cols``  - ``{column: (n,) float64 array}`` for every column the header
      declares, driven off the header rather than a fixed list, so this reads
      ``summary.mw.csv`` as happily as the GF summaries
    * ``meta``  - ``{key: str}`` from the trailing ``# key,value`` footer
    * ``total`` - ``{column: float}`` for the ``TOTAL`` row's populated columns

    Two details of the format are load-bearing. The ``TOTAL`` row leaves the
    state-count columns empty, so a naive parse either fails or silently records
    zero - only non-empty fields are collected; and in ``summary.mw.csv`` the
    TOTAL row is *shorter* than the header, so it has to be recognised before any
    length check. A footer value may itself contain commas
    (``# note,MW does not separate arc-start / data-gap / slip;``), so the split
    has to be bounded.

    The footer is a plain key/value map, and a key that appears twice keeps its
    last value - ``summary.mw.csv`` carries two ``# note`` lines and only the
    second survives.
    """
    sat: list[str] = []
    cols: dict[str, list[float]] = {}
    meta: dict[str, str] = {}
    total: dict[str, float] = {}
    header: list[str] | None = None

    with open(fn, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            s = ln.strip()
            if not s:
                continue
            if s.startswith("#"):
                body = s.lstrip("#").strip()
                if "," in body:
                    k, v = body.split(",", 1)   # bounded: the value may hold commas
                    meta[k.strip()] = v.strip()
                continue

            parts = [p.strip() for p in s.split(",")]

            if header is None:
                header = parts
                cols = {name: [] for name in header[1:]}
                continue

            # The TOTAL row is tested BEFORE the length check: it is itself a
            # short row in some products. summary.mw.csv has a four-column header
            # and a three-field TOTAL (`TOTAL,1068,21`), so a length check first
            # would silently drop the run's totals.
            if parts[0] == "TOTAL":
                for name, raw in zip(header[1:], parts[1:]):
                    if raw:
                        try:
                            total[name] = float(raw)
                        except ValueError:
                            pass
                continue

            if len(parts) < len(header):
                continue

            try:
                values = [float(p) for p in parts[1:len(header)]]
            except ValueError:
                continue

            sat.append(parts[0])
            for name, v in zip(header[1:], values):
                cols[name].append(v)

    return (np.asarray(sat),
            {k: np.asarray(v, dtype=float) for k, v in cols.items()},
            meta, total)


def load_gf_detector(fn: str):
    """Read one ``<sat>.gf.diff`` / ``.gf.poly`` / ``.mw`` file.

    Returns ``(sat, cols, status)``:

    * ``sat``    - the satellite id, repeated on every row
    * ``cols``   - ``{column: array}`` for every column except the last
    * ``status`` - (n,) array of the LAST column, the judgement token, verbatim

    The column spec comes from the file's own leading ``#`` comment rather than
    from a caller-supplied kind, which is what lets one reader serve all three
    products and any column added later. The comment lines must be skipped
    *before* splitting: they contain commas and semicolons.

    ``nan`` needs no special case - ``float("nan")`` succeeds and yields a float
    nan, which is what the untested epochs (arc start, gap, warmup) carry. That
    is a real omission only in appearance; there is no token to map.
    """
    names: list[str] | None = None
    cols: dict[str, list] = {}
    status: list[str] = []
    sat = ""

    with open(fn, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            s = ln.strip()
            if not s:
                continue
            if s.startswith("#"):
                if names is None:
                    names = s.lstrip("#").strip().split()
                continue
            if names is None:
                continue

            parts = s.split()
            if len(parts) < len(names):
                continue

            ncol = len(names) - 1          # the judgement is the last declared column
            if not cols:
                cols = {name: [] for name in names[:ncol]}

            row: dict[str, object] = {}
            ok = True
            for name, raw in zip(names[:ncol], parts[:ncol]):
                if name in _TEXT_COLUMNS:
                    row[name] = raw
                else:
                    try:
                        row[name] = float(raw)   # "nan" lands here, and is fine
                    except ValueError:
                        ok = False
                        break
            if not ok:
                continue

            sat = row.get("sat", sat) or sat
            for name in names[:ncol]:
                cols[name].append(row[name])
            status.append(parts[ncol])

    out = {}
    for name, values in cols.items():
        if name in _TEXT_COLUMNS:
            out[name] = np.asarray(values, dtype=str)
        else:
            out[name] = np.asarray(values, dtype=float)
    return str(sat), out, np.asarray(status, dtype=str)


def load_slip_manifest(fn: str):
    """Read the ground truth ``*.slips.csv`` written by the slip injector.

    Returns nine parallel arrays in manifest row order::

        sat, year, doy, sod, label, dN1, dN2, expected_dLI_m, expect

    ``sat``, ``label`` and ``expect`` are string arrays; ``expect`` is either
    ``SLIP`` or the adversarial state the detector should have reported instead
    (``INIT``, ``GAP``). The rest are float64. Columns are located by the header
    names, so their order in the file is not part of the contract. A row that is
    short or has an unparseable required field is skipped rather than aborting
    the read.

    ``csv.reader`` rather than ``csv.DictReader``: the latter yields ``None`` for
    a field missing from a short row, which turns a malformed line into a
    ``TypeError`` several frames away instead of a row that can be skipped.
    """
    required = ("sat", "year", "doy", "sod", "label",
                "dN1", "dN2", "expected_dLI_m", "expect")
    text_keys = ("sat", "label", "expect")
    out: dict[str, list] = {k: [] for k in required}

    with open(fn, "r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.reader(f)
        try:
            header = [h.strip() for h in next(reader)]
        except StopIteration:
            header = []
        index = {name: header.index(name) for name in required if name in header}
        if len(index) < len(required):
            return tuple(np.asarray([], dtype=str if k in text_keys else float)
                         for k in required)

        for row in reader:
            if len(row) < len(header):
                continue
            # Convert into a scratch row first: appending as we go would leave the
            # parallel arrays different lengths when a later field fails.
            try:
                parsed = {
                    "sat": row[index["sat"]].strip(),
                    "label": row[index["label"]].strip(),
                    "expect": row[index["expect"]].strip(),
                    "year": float(row[index["year"]]),
                    "doy": float(row[index["doy"]]),
                    "sod": float(row[index["sod"]]),
                    "dN1": float(row[index["dN1"]]),
                    "dN2": float(row[index["dN2"]]),
                    "expected_dLI_m": float(row[index["expected_dLI_m"]]),
                }
            except ValueError:
                continue
            for k in required:
                out[k].append(parsed[k])

    return tuple(np.asarray(out[k], dtype=str if k in text_keys else float)
                 for k in required)
