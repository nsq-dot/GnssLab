#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The double-difference construction, checked without any RINEX data.

RTK's difference stage is pure linear algebra over an equation system, so it can
be tested on hand-built inputs. That matters because the other RTK test -
test_rtk_float_regression.py - needs a 190 MB dataset that is gitignored and
therefore never present in CI. This one runs on a fresh clone: it drives
examples/diff_station, which has no file I/O and ships its fixtures in the
binary.

What it pins, and why each one is worth a test:

  differenceStation()
    - drops every Parameter::iono coefficient. Not a difference of ionospheric
      delays: the parameter is deleted outright, which is the short-baseline
      assumption dI -> 0 taken literally.
    - combines the weights as 1 / (1/w_rover + 1/w_base). Getting this wrong
      (averaging, or summing) silently mis-weights every epoch.

  differenceSat()
    - removes the receiver clock, which is the whole point of the second
      difference.
    - carries the ambiguity coefficient through UNCHANGED. The variable is named
      after satellite i but holds the DD ambiguity N_i - N_datum; the test
      checks the coefficient is still a wavelength rather than a difference.
    - leaves a full-column-rank system for well-posed geometry, and a deficient
      one when two satellites share a line of sight.

Needs diff_station built. Missing it reports SKIP:

    python tests/test_rtk_equations.py
    python tests/test_rtk_equations.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(f"  {'ok  ' if cond else 'FAIL'}  {msg}")
    if not cond:
        failures.append(msg)


def find_exe(name: str) -> str | None:
    for sub in ("build/bin", "build-debug/bin", "cmake-build-debug/bin", "cmake-build-debug", "bin"):
        for suffix in (".exe", ""):
            p = os.path.join(ROOT, sub, name + suffix)
            if os.path.isfile(p):
                return p
    return shutil.which(name)


def summaries(text: str) -> list[dict]:
    """Collect every `SUMMARY key=value ...` line into a dict."""
    out = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("SUMMARY "):
            continue
        d = {}
        for tok in line.split()[1:]:
            k, _, v = tok.partition("=")
            d[k] = v
        out.append(d)
    return out


# A minimal well-posed system: three satellites, four observation types, one of
# which is the datum. Two non-datum satellites with different geometry give the
# three coordinates independent information.
# Two pseudoranges and two carriers per satellite, as a real dual-frequency
# solution has. That matters: with only one code and one phase per satellite the
# system is under-determined (4 observations, 5 unknowns) and there would be no
# well-posed case to contrast the rank-deficient one against.
# Three non-datum satellites, not two. With only two, the double differences
# span two directions in space, the coordinate is determined only within the
# plane they contain, and the system is rank deficient by one no matter how well
# the satellites are spread. The first version of this fixture used two and the
# "well-posed" case failed its own rank check.
_GOOD_ROW = {
    "G08": "dx=0.364 dy=-0.414 dz=-0.834",   # the datum
    "G21": "dx=0.742 dy=-0.660 dz=-0.118",
    "G27": "dx=-0.150 dy=-0.685 dz=-0.713",
    "G30": "dx=0.946 dy=0.194 dz=-0.258",
}
_BAD_ROW = dict(_GOOD_ROW)
_BAD_ROW["G27"] = "dx=0.742 dy=-0.660 dz=-0.118"   # same as G21: collapses


def _fixture(rows: dict) -> str:
    out = ["rover"]
    for sat, grad in rows.items():
        out.append(f"{sat} C1  6.000 10.0 {grad} cdt=1.0 iono=1.0")
        out.append(f"{sat} C2  6.400 10.0 {grad} cdt=1.0 iono=1.647")
        out.append(f"{sat} L1  5.900 100.0 {grad} cdt=1.0 iono=-1.0 amb=0.190")
        out.append(f"{sat} L2  6.300 100.0 {grad} cdt=1.0 iono=-1.647 amb=0.244")
    out.append("base")
    for sat in rows:
        out.append(f"{sat} C1  5.900 10.0")
        out.append(f"{sat} C2  6.300 10.0")
        out.append(f"{sat} L1  5.800 100.0")
        out.append(f"{sat} L2  6.200 100.0")
    out.append("datum G08")
    return "\n".join(out) + "\n"


WELL_POSED = _fixture(_GOOD_ROW)
RANK_DEFICIENT = _fixture(_BAD_ROW)


def run_diff_station(exe: str, stdin_text: str) -> subprocess.CompletedProcess:
    return subprocess.run([exe], cwd=ROOT, input=stdin_text,
                          capture_output=True, text=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binary is missing")
    args = ap.parse_args()

    exe = find_exe("diff_station")
    if not exe:
        print("SKIP  diff_station not built - run `gnss build` (or cmake --build build)")
        return 1 if args.required else 0

    #-----------------------------------------------------------------
    print("Well-posed system: 3 satellites x 2 frequencies, datum G08\n")
    r = run_diff_station(exe, WELL_POSED)
    check(r.returncode == 0, f"diff_station exits 0 (got {r.returncode})")
    if r.returncode != 0:
        print(r.stdout[-1500:])
        print(r.stderr[-1500:], file=sys.stderr)
        return 1

    s = summaries(r.stdout)
    check(len(s) == 1, f"exactly one SUMMARY line (got {len(s)})")
    if not s:
        return 1
    d = s[0]

    n_other = 3          # G21, G27, G30 - the satellites that are not the datum
    n_phase = 2          # C1/C2 -> L1/L2
    n_code = 2

    check(d["ionoInSD"] == "0", "differenceStation drops Parameter::iono")
    check(d["clockInDD"] == "0", "differenceSat drops the receiver clock")
    check(d["datum"] == "G08", "the requested datum satellite is used")
    check(int(d["ambiguities"]) == n_other * n_phase,
          f"one DD ambiguity per non-datum satellite per frequency "
          f"({d['ambiguities']}, want {n_other * n_phase})")
    check(int(d["nUnk"]) == 3 + n_other * n_phase,
          f"unknowns = 3 coordinates + {n_other * n_phase} ambiguities ({d['nUnk']})")
    check(int(d["nObs"]) == n_other * (n_code + n_phase),
          f"observations = {n_other} satellites x {n_code + n_phase} types ({d['nObs']})")
    check(int(d["dof"]) == n_other * n_code - 3,
          f"redundancy = {n_other} x {n_code} code - 3 ({d['dof']})")
    check(d["rank"] == d["nUnk"],
          f"full column rank ({d['rank']}/{d['nUnk']})")

    # The weight combination is the only arithmetic in differenceStation that
    # is not bookkeeping, so check the number itself:
    #   rover weight = base weight = 10  ->  1/(1/10 + 1/10) = 5
    # SD table columns are: sat type prefit_rover prefit_base prefit_SD w_rover w_base w_SD
    sd_rows = [l.split() for l in r.stdout.splitlines()]
    sd_rows = [f for f in sd_rows if len(f) == 8 and f[0] == "G21" and f[1] == "C1"]
    check(bool(sd_rows) and sd_rows[0][7] == "5.0000",
          "SD weight is 1/(1/w_rover + 1/w_base) = 5 for two weight-10 equations"
          + (f" (got {sd_rows[0][7]})" if sd_rows else " (row not found)"))

    # The DD ambiguity keeps the wavelength as its coefficient - it is NOT
    # differenced, even though the unknown it multiplies is a difference.
    check("ambiguity[G21]=0.1900" in r.stdout,
          "DD ambiguity coefficient is carried through unchanged "
          "(0.1900 m = the L1 wavelength, not a difference of wavelengths)")
    check("ambiguity[G21]=0.2440" in r.stdout,
          "... and likewise for L2 (0.2440 m)")

    #-----------------------------------------------------------------
    print("\nRank-deficient system: G27 shares G21's line of sight\n")
    r = run_diff_station(exe, RANK_DEFICIENT)
    check(r.returncode == 0, f"diff_station exits 0 (got {r.returncode})")
    s = summaries(r.stdout)
    check(len(s) == 1, f"exactly one SUMMARY line (got {len(s)})")
    if s:
        d = s[0]
        check(int(d["rank"]) < int(d["nUnk"]),
              f"rank deficiency detected ({d['rank']} < {d['nUnk']})")
        check("RANK DEFICIENT" in r.stdout,
              "the rank-deficient case is reported, not silently solved")
        # The two fixtures carry the same satellites and the same number of
        # equations, so the only thing that differs is the geometry. That is
        # what makes the pair a controlled comparison rather than two unrelated
        # inputs.
        check(int(d["nObs"]) == n_other * (n_code + n_phase),
              f"same equation count as the well-posed case ({d['nObs']})")
        check(int(d["nUnk"]) == 3 + n_other * n_phase,
              f"same unknown count as the well-posed case ({d['nUnk']})")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("RTK difference-stage checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
