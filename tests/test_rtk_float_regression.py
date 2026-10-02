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

## The `--fix` half

The same dataset makes the fixed solution checkable in a way few datasets allow:
the zero baseline's true answer is known independently of any solver, because
both receivers see one antenna. So this test does not merely assert that a fixed
solution came out - it asserts that it is BETTER than the same epoch's float one,
and that the float column of the fixed file reproduces the float file exactly.

Both halves need `data/Zero-baseline/`, which is a download, so neither runs in
CI - see tests/README.md.

Needs `rtk` built and data/Zero-baseline/ present (it is gitignored, ~95 MB
per file). Missing either one is reported and skipped:

    python tests/test_rtk_float_regression.py
    python tests/test_rtk_float_regression.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import csv
import json
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


def read_diag_columns(path: str, names):
    """Read named columns out of a diagnostics CSV, as a tuple of lists.

    Columns are located by the file's own header, so a column added later does
    not shift the ones asked for here.
    """
    out = {n: [] for n in names}
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.DictReader(f)
        header = [h.strip() for h in (reader.fieldnames or [])]
        if not all(n in header for n in names):
            return tuple(None for _ in names)
        for row in reader:
            try:
                for n in names:
                    out[n].append(float(row[n]))
            except (TypeError, ValueError):
                for n in names:
                    out[n].pop()
    return tuple(out[n] for n in names)


def run_rtk(exe: str, sys_name: str, out_dir: str,
            *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [exe, os.path.join(ROOT, "config", "rtk.ini"),
         "--sys", sys_name, "--stop", STOP, "--out-dir", out_dir, *extra],
        cwd=ROOT, capture_output=True, text=True)


def parse_fixed_solution(path: str):
    """Read the `--fix` output: (sod, spp, float, ratio, fixed).

    The layout is the textbook main program's - `spp: X Y Z float-rtk: X Y Z
    ratio:R fixed-rtk:X Y Z` - and note that `ratio:` and `fixed-rtk:` are glued
    to their values with no space, exactly as the notes print them. So the
    fields are located by name rather than by column position.
    """
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.split()
            if not any(t.startswith("fixed-rtk:") for t in p):
                continue
            try:
                i = p.index("spp:")
                j = p.index("float-rtk:")
                k = [n for n, t in enumerate(p) if t.startswith("ratio:")][0]
                m = [n for n, t in enumerate(p) if t.startswith("fixed-rtk:")][0]
                rows.append((
                    float(p[2]),
                    tuple(float(p[i + 1 + n]) for n in range(3)),
                    tuple(float(p[j + 1 + n]) for n in range(3)),
                    float(p[k][len("ratio:"):]),
                    (float(p[m][len("fixed-rtk:"):]),
                     float(p[m + 1]), float(p[m + 2])),
                ))
            except (ValueError, IndexError):
                continue
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binary or data is missing")
    args = ap.parse_args()

    exe = find_exe("rtk")
    if not exe:
        print("SKIP  rtk not built - run `gnss build` (or cmake --build build)")
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
        check(False, f"rtk exited {r.returncode}")
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

    # A run without --fix must not write the fixed file at all - not an empty
    # one. Otherwise "was fixing on?" could not be told from the output.
    check(not os.path.isfile(os.path.join(out, ROVER + "_gps_rtk_fixed.out")),
          "no fixed-solution file is written when --fix is off")
    # The fixing columns are present in the diagnostics either way, so that the
    # header does not depend on the flags and the Python reader never has to
    # special-case a run.
    with open(diag, "r", encoding="utf-8") as f:
        header = f.readline().strip()
    check(all(c in header.split(",") for c in ("nAmb", "ratio", "fixed", "absDxyzFixed")),
          "the diagnostics header carries the fixing columns even with --fix off")

    #-----------------------------------------------------------------
    # GPS with --fix: float against fixed, on the dataset whose truth is known
    #-----------------------------------------------------------------
    # The zero baseline is the only place in the project where the right answer
    # is known independently of any solver: both receivers see the same antenna,
    # so the rover's true position is the base's, strictly. That makes it
    # possible to assert not just that a fixed solution came out but that it is
    # BETTER, which is the whole claim fixing makes.
    print()
    print("GPS with --fix - the fixed solution against the same epoch's float\n")
    fixdir = os.path.join(out, "fixed")
    os.makedirs(fixdir, exist_ok=True)
    rf = run_rtk(exe, "gps", fixdir, "--fix")
    check(rf.returncode == 0, f"--fix run exits 0 (got {rf.returncode})")
    if rf.returncode != 0:
        print(rf.stdout[-2000:])
        print(rf.stderr[-2000:], file=sys.stderr)
        return 1

    fixpath = os.path.join(fixdir, ROVER + "_gps_rtk_fixed.out")
    check(os.path.isfile(fixpath), "fixed-solution file written")
    if os.path.isfile(fixpath):
        fx = parse_fixed_solution(fixpath)
        check(len(fx) == EXPECTED_EPOCHS,
              f"{len(fx)} epochs in the fixed file (want {EXPECTED_EPOCHS})")

        if fx and got:
            # The float column of the fixed file must be the float file's rtk:
            # column - same run, same input, so any difference is a bug in the
            # writer rather than a numerical one.
            same = sum(1 for a, b in zip(fx, got)
                       if all(a[2][i] == b[2][i] for i in range(3)))
            check(same == len(fx),
                  f"float-rtk: reproduces the float run's rtk: column on "
                  f"{same}/{len(fx)} epochs")

        base_pos = approx_position_of(os.path.join(ZERO, BASE))
        if base_pos and fx:
            n_fixed = sum(1 for row in fx if row[3] > 3.0)
            check(n_fixed == len(fx),
                  f"all {len(fx)} epochs clear ratio > 3 (got {n_fixed})")
            worst_f = max(distance(row[2], base_pos) for row in fx)
            worst_x = max(distance(row[4], base_pos) for row in fx)
            # Measured on this window: float 2.4 m worst, fixed 15 mm. The bound
            # is loose enough to survive a different build's rounding and still
            # orders of magnitude tighter than the float solution.
            check(worst_x <= 0.10,
                  f"fixed position within 0.10 m of the true position "
                  f"(worst {worst_x:.4f} m)")
            check(worst_x < worst_f,
                  f"fixed beats float on the worst epoch "
                  f"({worst_x:.4f} m vs {worst_f:.4f} m)")

    # A threshold below 1 would accept every fix unconditionally, which would
    # look like a result rather than a bug. Refused at the command line.
    bad = run_rtk(exe, "gps", fixdir, "--ratio", "0.5")
    check(bad.returncode == 2, f"--ratio 0.5 is rejected (exit {bad.returncode})")

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
                check(False, f"{sys_name}: rtk exited {r.returncode}")
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

    #-----------------------------------------------------------------
    # The mixed BeiDou solution and its inter-system bias
    #-----------------------------------------------------------------
    # Structural, like the BeiDou block above: there is no earlier mixed run to
    # anchor against. What is checked is that merging the generations is
    # possible at all (which needs the per-satellite frequency pair), that the
    # bias column is absent without --isb and present with it, and that the
    # estimated bias stays at the centimetre level - on this dataset it is about
    # -12 mm, and a metre would mean the parameter is absorbing something else.
    print()
    print("Mixed BDS-2 + BDS-3 - merged solution and the inter-system bias\n")
    # One output directory per run: they write the same filenames.
    n_dd = {}
    isb_col = {}
    for tag, extra in (("plain", ()), ("isb", ("--isb",))):
        d = os.path.join(out, "mixed-" + tag)
        os.makedirs(d, exist_ok=True)
        label = "bds23" + (" --isb" if extra else "")
        rm = run_rtk(exe, "bds23", d, *extra)
        check(rm.returncode == 0, f"{label} exits 0 (got {rm.returncode})")
        if rm.returncode != 0:
            print(rm.stdout[-2000:])
            print(rm.stderr[-2000:], file=sys.stderr)
            return 1

        rows = parse_solution(os.path.join(d, ROVER + "_bds23_rtk_float.out"))
        check(len(rows) == EXPECTED_EPOCHS,
              f"{label}: {len(rows)} epochs (want {EXPECTED_EPOCHS})")
        if rows:
            check(all(math.isfinite(v) for _, s, t in rows for v in s + t),
                  f"{label}: all coordinates finite")
            if base_pos:
                worst = max(distance(t, base_pos) for _, _, t in rows)
                check(worst <= RTK_SANITY_M,
                      f"{label}: within {RTK_SANITY_M:.0f} m of the base header "
                      f"(worst {worst:.3f} m)  [structural, NOT an anchor]")
        n_dd[tag], isb_col[tag] = read_diag_columns(
            os.path.join(d, ROVER + "_bds23_rtk_diag.csv"), ("nDD", "isb"))

    # Merging must actually merge. A per-satellite frequency pair is what makes
    # this possible, so an empty or single-generation double-difference system
    # is exactly the failure this checks for.
    if n_dd.get("plain"):
        check(min(n_dd["plain"]) >= 4,
              f"bds23: every epoch has double differences "
              f"(min {min(n_dd['plain'])}, max {max(n_dd['plain'])})")

    check(isb_col.get("plain") is not None
          and all(v == 0.0 for v in isb_col["plain"]),
          "no inter-system bias is estimated without --isb")
    nonzero = [v for v in isb_col.get("isb", []) if v != 0.0]
    check(len(nonzero) > 0,
          f"the bias IS estimated with --isb ({len(nonzero)} epochs)")
    if nonzero:
        worst_isb = max(abs(v) for v in nonzero)
        check(worst_isb < 0.5,
              f"every estimated bias is under 0.5 m (worst {worst_isb:.3f} m) - "
              "a metre here would mean the parameter is absorbing something "
              "other than the receiver bias")

    #-----------------------------------------------------------------
    # The Kalman filter, and the cycle-slip coverage it depends on
    #-----------------------------------------------------------------
    print()
    print("Kalman filter - the float solution the filter buys\n")
    kal_dir = os.path.join(out, "kalman")
    os.makedirs(kal_dir, exist_ok=True)
    rk = run_rtk(exe, "gps", kal_dir, "--estimator", "kalman")
    check(rk.returncode == 0, f"--estimator kalman exits 0 (got {rk.returncode})")
    if rk.returncode == 0:
        krows = parse_solution(os.path.join(kal_dir, ROVER + "_gps_rtk_float.out"))
        check(len(krows) == EXPECTED_EPOCHS,
              f"kalman: {len(krows)} epochs (want {EXPECTED_EPOCHS})")
        if krows and got and base_pos:
            # The whole point of the filter: tying the ambiguities across epochs
            # is what lets the carrier phase constrain the position, so the FLOAT
            # solution improves. The fixed solution does not move - both converge
            # to the same integers - which is why this is asserted on the float
            # column only. Measured: 2.4 m to 0.04 m worst on this window.
            worst_l = max(distance(t, base_pos) for _, _, t in got)
            worst_k = max(distance(t, base_pos) for _, _, t in krows)
            check(worst_k < worst_l,
                  f"kalman float beats least-squares float on the worst epoch "
                  f"({worst_k:.4f} m vs {worst_l:.4f} m)")
        # The filter's premise is that an ambiguity is constant across epochs,
        # which needs a slip flag on every one of them.
        ndd, uncovered = read_diag_columns(
            os.path.join(kal_dir, ROVER + "_gps_rtk_diag.csv"), ("nDD", "nAmb"))
        check(ndd is not None, "kalman run wrote diagnostics")
        with open(os.path.join(kal_dir, ROVER + "_gps_manifest.json"),
                  "r", encoding="utf-8") as f:
            kman = json.load(f)
        check(kman.get("estimator") == "kalman",
              f"the manifest records the estimator ({kman.get('estimator')})")
        check(kman.get("csAmbiguitiesWithoutFlag") == 0,
              "every ambiguity the filter carries has a cycle-slip flag "
              f"({kman.get('csAmbiguitiesWithoutFlag')} without)")

    # The bands the detectors use have to follow the mode. BeiDou B2a is the case
    # that forced this: the detectors' default table is B1I/B2I, so before the
    # band list was settable a `bds3` run produced no flags at all - and a filter
    # with no flags carries a stale ambiguity across every slip, which is exactly
    # how the first Kalman run diverged to 14 m at sod 26538.
    for sysname in ("bds3", "bds23"):
        d = os.path.join(out, "cs-" + sysname)
        os.makedirs(d, exist_ok=True)
        rc2 = subprocess.run(
            [exe, os.path.join(ROOT, "config", "rtk.ini"), "--sys", sysname,
             "--dump-cs", "--stop", STOP, "--out-dir", d],
            cwd=ROOT, capture_output=True, text=True)
        check(rc2.returncode == 0, f"--sys {sysname} --dump-cs exits 0")
        m = [f for f in os.listdir(d) if f.endswith("_manifest.json")]
        if m:
            with open(os.path.join(d, m[0]), "r", encoding="utf-8") as f:
                man = json.load(f)
            check(man.get("csAmbiguitiesWithoutFlag") == 0,
                  f"{sysname}: every ambiguity has a flag "
                  f"(bands missing: {man.get('csBandsNotCovered')})")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("RTK regression passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
