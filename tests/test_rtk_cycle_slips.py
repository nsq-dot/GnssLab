#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The cycle-slip flags the Kalman solver consumes, checked against known slips.

``apps/rtk_float --dump-cs`` runs the chapter-7 detectors on both receivers,
merges them through the flag-carrying ``differenceStation`` overload, and writes
the flags keyed by the ambiguity variables the filter carries. This test is what
says that wiring works end to end - the keys reach the ambiguity variables, and
the values reach the epochs where slips actually happened.

Two things it checks that nothing else can:

**The keys.** Every double-difference ambiguity in the epoch's equation system
must have a flag entry. An ambiguity with no entry is one the filter would carry
across a slip as if nothing had happened, silently, for the rest of the run. The
count comes out of the run's manifest (``csAmbiguitiesWithoutFlag``).

**The values.** Slips are injected into the rover file at known epochs by
``scripts/inject_cycle_slips.py --plan auto``, and every one of them must be
flagged. The plan deliberately includes a ``(77, 60)`` cycle pair, which is the
geometry-free combination's exact null space - GF cannot see it at all. It being
detected is the whole reason the adapter takes the UNION of GF and MW rather
than either alone.

Note what a "false alarm" here is not. The zero-baseline data has real cycle
slips of its own - chapter 7 measured several satellites with hundreds - and the
injected truth knows nothing about them. So the extra flags are reported, not
asserted against; only their overall rate is bounded, loosely.

Needs `rtk_float` built and data/Zero-baseline/ present (gitignored, ~95 MB per
file), so like the other RTK tests it does not run in CI:

    python tests/test_rtk_cycle_slips.py
    python tests/test_rtk_cycle_slips.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ZERO = os.path.join(ROOT, "data", "Zero-baseline")
ROVER = "oem719-202203031500-1.obs"
BASE = "oem719-202203031500-2.obs"
NAV = "BRDC00IGS_R_20220620000_01D_MN.rnx"

#: How many (satellite, epoch) pairs may be flagged outside the injected set.
#: Measured at 5.4% of the run, almost all of it real slips in the data; the
#: bound is loose because that number is a property of the dataset, not of the
#: wiring, and this test is about the wiring.
MAX_FALSE_FLAG_FRACTION = 0.15

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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binary or data is missing")
    args = ap.parse_args()

    exe = find_exe("rtk_float")
    if not exe:
        print("SKIP  rtk_float not built - run `gnss build` (or cmake --build build)")
        return 1 if args.required else 0

    needed = [os.path.join(ZERO, n) for n in (ROVER, BASE, NAV)]
    missing = [p for p in needed if not os.path.isfile(p)]
    if missing:
        print(f"SKIP  zero-baseline data missing from {ZERO}:")
        for m in missing:
            print(f"        {os.path.basename(m)}")
        return 1 if args.required else 0

    work = os.path.join(ROOT, "output", "_rtk_cs")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)

    #-----------------------------------------------------------------
    print("Injecting known slips into the rover file\n")
    injected = os.path.join(work, "oem719-injected.obs")
    truth_path = os.path.join(work, "truth.csv")
    r = subprocess.run(
        [sys.executable, os.path.join(ROOT, "scripts", "inject_cycle_slips.py"),
         "--obs", os.path.join(ZERO, ROVER), "--out", injected,
         "--manifest", truth_path, "--plan", "auto"],
        cwd=ROOT, capture_output=True, text=True)
    check(r.returncode == 0, f"inject_cycle_slips exits 0 (got {r.returncode})")
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:], file=sys.stderr)
        return 1

    with open(truth_path, "r", encoding="utf-8", newline="") as f:
        truth = list(csv.DictReader(f))
    check(len(truth) > 0, f"{len(truth)} slips injected")
    expect_slip = [t for t in truth if t.get("expect", "").strip().upper() != "INIT"]
    check(len(expect_slip) > 0, f"{len(expect_slip)} of them expect a SLIP decision")

    #-----------------------------------------------------------------
    print("\nRunning the solver with --dump-cs on the injected rover\n")
    run_dir = os.path.join(work, "run")
    os.makedirs(run_dir, exist_ok=True)
    r = subprocess.run(
        [exe, os.path.join(ROOT, "config", "rtk.ini"), "--sys", "gps",
         "--dump-cs", "--obs", injected, "--out-dir", run_dir],
        cwd=ROOT, capture_output=True, text=True)
    check(r.returncode == 0, f"rtk_float --dump-cs exits 0 (got {r.returncode})")
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:], file=sys.stderr)
        return 1

    cs_files = [f for f in os.listdir(run_dir) if f.endswith("_cs.csv")]
    check(len(cs_files) == 1, f"one cycle-slip file written (got {len(cs_files)})")
    if not cs_files:
        print("\n1 check(s) FAILED")
        return 1

    with open(os.path.join(run_dir, cs_files[0]), "r", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    flagged = {(row["sat"], float(row["sod"])) for row in rows}
    check(len(flagged) > 0, f"{len(flagged)} distinct (satellite, epoch) pairs flagged")

    #-----------------------------------------------------------------
    print("\nEvery injected slip must be flagged\n")
    hits, misses = [], []
    for t in expect_slip:
        key = (t["sat"], float(t["sod"]))
        (hits if key in flagged else misses).append(t)

    for t in misses:
        print(f"       missed: {t['sat']} at sod {t['sod']} "
              f"({t['label']}, dN=({t['dN1']}, {t['dN2']}))")
    check(not misses, f"detected {len(hits)}/{len(expect_slip)} injected slips")

    # The one case GF cannot see, by construction: lambda1*77 - lambda2*60 = 0.
    # If this fails, the adapter has stopped taking the union of the two
    # detectors and is running GF alone.
    nullspace = [t for t in expect_slip if t["label"] == "gf-null-space"]
    check(bool(nullspace), "the injection plan includes the GF null-space case")
    for t in nullspace:
        check((t["sat"], float(t["sod"])) in flagged,
              f"the GF null space ({t['dN1']}, {t['dN2']}) on {t['sat']} IS detected "
              "- only the MW half of the union can see it")

    #-----------------------------------------------------------------
    print("\nEvery ambiguity the filter carries must have a flag\n")
    manifests = [f for f in os.listdir(run_dir) if f.endswith("_manifest.json")]
    check(len(manifests) == 1, f"one manifest written (got {len(manifests)})")
    if manifests:
        with open(os.path.join(run_dir, manifests[0]), "r", encoding="utf-8") as f:
            manifest = json.load(f)
        uncovered = manifest.get("csAmbiguitiesWithoutFlag")
        check(uncovered == 0,
              f"no double-difference ambiguity is left without a flag "
              f"(got {uncovered}, bands {manifest.get('csBandsNotCovered')})")
        check(manifest.get("csEpochsWithSlip", 0) > 0,
              f"the run has epochs with a slip ({manifest.get('csEpochsWithSlip')})")

    #-----------------------------------------------------------------
    print("\nHow many flags fall outside the injected set\n")
    injected_keys = {(t["sat"], float(t["sod"])) for t in truth}
    extra = flagged - injected_keys
    fraction = len(extra) / max(1, int(manifest.get("epochs", 1)))
    print(f"       {len(extra)} pair(s), {100.0 * fraction:.1f}% of the run")
    print("       (the zero-baseline data has real slips of its own, so most of")
    print("        these are detections the injection truth knows nothing about)")
    check(fraction <= MAX_FALSE_FLAG_FRACTION,
          f"flagged pairs stay under {100 * MAX_FALSE_FLAG_FRACTION:.0f}% of the run "
          f"({100.0 * fraction:.1f}%)")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("RTK cycle-slip wiring passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
