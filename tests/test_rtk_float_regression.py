#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regression for the RTK float solution against the original program's output.

`data/Zero-baseline/oem719-202203031500-1.obs.rtk.lsq.out` was produced by the
original gnssLab-2.2 program on the same zero-baseline pair, and its 25 epochs
(sod 24517..24541) are exactly the window `--stop 2022-03-03T06:49:00` selects.
That makes it a genuine external anchor: an independent implementation, run
years earlier, on the same inputs.

## Why only the `rtk:` columns are compared byte-for-byte

Each line carries two positions:

    ... spp: X Y Z rtk: X Y Z

The `rtk:` column reproduces the original **exactly**, on all 25 lines, and is
asserted byte-for-byte. The `spp:` column does not: it differs by up to ~7 m.

That is not a numerical bug in the usual sense. The rover's single-point solve
carries one free float ambiguity per (satellite, frequency), so for any position
the ambiguities can absorb the phase residuals exactly - the normal matrix is
singular in those directions and `inverse()` amplifies rounding noise. Two builds
of the same source agree with each other to the last digit; two different builds
disagree by metres. The double-difference solve re-references the position, which
is why the `rtk:` column is stable despite the `spp:` one not being.

So `spp:` gets a tolerance, generous enough to stay green across compilers and
tight enough that a real regression - a wrong sign, a dropped satellite, a
changed mask - still fails. Both facts are asserted rather than one being hidden.

Needs `rtk_float` built and data/Zero-baseline/ present (it is gitignored, ~95 MB
per file). Missing either one is reported and skipped:

    python tests/test_rtk_float_regression.py
    python tests/test_rtk_float_regression.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ZERO = os.path.join(ROOT, "data", "Zero-baseline")
ROVER = "oem719-202203031500-1.obs"
BASE = "oem719-202203031500-2.obs"
ANCHOR = "oem719-202203031500-1.obs.rtk.lsq.out"

# The window that reproduces the anchor's 25 epochs. The anchor's last line is
# sod 24541, one past this, because the loop stops after writing the epoch that
# exceeds the stop time.
STOP = "2022-03-03T06:49:00"
EXPECTED_EPOCHS = 25

# Generous: the real disagreement is ~7 m and comes from an ill-conditioned
# single-point solve, not from the RTK result. See the module docstring.
SPP_TOLERANCE_M = 15.0

# For the BeiDou runs there is no anchor. A loose sanity gate on the double
# difference against the base station's own header position, in metres.
RTK_SANITY_M = 5.0

failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(f"  {'ok  ' if cond else 'FAIL'}  {msg}")
    if not cond:
        failures.append(msg)


def normalise_eol(data: bytes) -> bytes:
    """Drop the CR of CRLF line endings - see test_regression_pipeline.py."""
    return data.replace(b"\r\n", b"\n")


def find_exe(name: str) -> str | None:
    for sub in ("build/bin", "build-debug/bin", "cmake-build-debug/bin", "cmake-build-debug", "bin"):
        for suffix in (".exe", ""):
            p = os.path.join(ROOT, sub, name + suffix)
            if os.path.isfile(p):
                return p
    return shutil.which(name)


def parse_solution(path: str) -> list[tuple[float, tuple, tuple]]:
    """Read `<sod> spp: X Y Z rtk: X Y Z` lines into (sod, spp, rtk) tuples."""
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            f_ = line.split()
            if len(f_) < 12:          # 0..3 prefix, 4 'spp:', 5-7, 8 'rtk:', 9-11
                continue
            rows.append((float(f_[2]),
                         tuple(float(v) for v in f_[5:8]),
                         tuple(float(v) for v in f_[9:12])))
    return rows


def approx_position_of(obs_path: str) -> tuple | None:
    """Read APPROX POSITION XYZ out of a RINEX observation header."""
    with open(obs_path, "r", encoding="utf-8", errors="replace") as f:
        for _ in range(200):
            line = f.readline()
            if not line:
                return None
            if line[60:80].strip() == "APPROX POSITION XYZ":
                return (float(line[0:14]), float(line[14:28]), float(line[28:42]))
    return None


def distance(a, b) -> float:
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def run_rtk(exe: str, sys_name: str, out_dir: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [exe, os.path.join(ROOT, "config", "rtk.ini"),
         "--sys", sys_name, "--stop", STOP, "--out-dir", out_dir],
        cwd=ROOT, capture_output=True, text=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binary or data is missing")
    args = ap.parse_args()

    exe = find_exe("rtk_float")
    if not exe:
        print("SKIP  rtk_float not built - run `gnss build` (or cmake --build build)")
        return 1 if args.required else 0

    needed = [os.path.join(ZERO, n) for n in (ROVER, BASE, ANCHOR)]
    needed.append(os.path.join(ZERO, "BRDC00IGS_R_20220620000_01D_MN.rnx"))
    missing = [p for p in needed if not os.path.isfile(p)]
    if missing:
        print(f"SKIP  zero-baseline data missing from {ZERO}:")
        for m in missing:
            print(f"        {os.path.basename(m)}")
        print("      (data/ is gitignored - see data/README.md)")
        return 1 if args.required else 0

    out = os.path.join(ROOT, "output", "_rtk_regression")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)

    #-----------------------------------------------------------------
    # GPS: the anchored comparison
    #-----------------------------------------------------------------
    print("GPS - comparing the rtk: column against the original program's output\n")
    r = run_rtk(exe, "gps", out)
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:], file=sys.stderr)
        check(False, f"rtk_float exited {r.returncode}")
        return 1

    got_path = os.path.join(out, ROVER + "_gps_rtk_float.out")
    if not os.path.isfile(got_path):
        check(False, f"solution not produced at {got_path}")
        return 1

    got = parse_solution(got_path)
    want = parse_solution(os.path.join(ZERO, ANCHOR))
    check(len(got) == EXPECTED_EPOCHS,
          f"produced {len(got)} epochs (want {EXPECTED_EPOCHS})")
    check(len(got) == len(want), f"epoch count matches the anchor ({len(want)})")

    if got and want and len(got) == len(want):
        check(all(abs(g[0] - w[0]) < 1e-6 for g, w in zip(got, want)),
              "every epoch time matches the anchor")

        n_exact = sum(1 for g, w in zip(got, want)
                      if all(g[2][i] == w[2][i] for i in range(3)))
        check(n_exact == len(want),
              f"rtk: position byte-identical to the anchor on {n_exact}/{len(want)} epochs")

        worst = max(distance(g[1], w[1]) for g, w in zip(got, want))
        check(worst <= SPP_TOLERANCE_M,
              f"spp: position within {SPP_TOLERANCE_M:.0f} m of the anchor "
              f"(worst {worst:.3f} m)")

    # The diagnostics are what make a bad epoch visible; a run without them is
    # not the product this test is meant to guard.
    diag = os.path.join(out, ROVER + "_gps_rtk_diag.csv")
    check(os.path.isfile(diag), "diagnostics file written")

    mf = os.path.join(out, ROVER + "_gps_manifest.json")
    check(os.path.isfile(mf), "manifest written")

    #-----------------------------------------------------------------
    # BeiDou: structural only, and said so
    #-----------------------------------------------------------------
    # There is no earlier BeiDou RTK run to anchor against, so these runs get
    # invariants, not a byte comparison. Both frequency pairs are exercised
    # because they select disjoint satellite sets.
    print()
    print("BeiDou - no anchor exists; checking structural invariants only\n")
    base_pos = approx_position_of(os.path.join(ZERO, BASE))
    if base_pos is None:
        check(False, "could not read the base station's header position")
    else:
        for sys_name in ("bds2", "bds3"):
            r = run_rtk(exe, sys_name, out)
            if r.returncode != 0:
                check(False, f"{sys_name}: rtk_float exited {r.returncode}")
                continue
            path = os.path.join(out, f"{ROVER}_{sys_name}_rtk_float.out")
            if not os.path.isfile(path):
                check(False, f"{sys_name}: solution not produced")
                continue
            rows = parse_solution(path)
            check(len(rows) == EXPECTED_EPOCHS,
                  f"{sys_name}: {len(rows)} epochs (want {EXPECTED_EPOCHS})")
            if not rows:
                continue
            finite = all(math.isfinite(v) for _, s, t in rows for v in s + t)
            check(finite, f"{sys_name}: all coordinates finite")
            worst = max(distance(t, base_pos) for _, _, t in rows)
            check(worst <= RTK_SANITY_M,
                  f"{sys_name}: within {RTK_SANITY_M:.0f} m of the base header "
                  f"(worst {worst:.3f} m)  [structural, NOT an anchor]")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("RTK regression passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
