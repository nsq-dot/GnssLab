#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""End-to-end regression: build, run on the sample, diff against the baseline.

This is the only test that exercises the whole chain - C++ compiler, solver,
output format, Python analysis - and it is what CI runs.

It needs `spp_if` to have been built and data/sample/ to exist. When either is
missing it reports that clearly and exits 0, so a contributor who has only
checked out the source is not greeted with a red failure for not having
downloaded data:

    python tests/test_regression_pipeline.py
    python tests/test_regression_pipeline.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

OBS = "WUH200CHN_R_20250010000_01D_30S_MO.rnx"
NAV = "BRDC00IGS_R_20250010000_01D_MN.rnx"

failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(f"  {'ok  ' if cond else 'FAIL'}  {msg}")
    if not cond:
        failures.append(msg)


def find_spp() -> str | None:
    for sub in ("build/bin", "build-debug/bin", "cmake-build-debug/bin", "cmake-build-debug", "bin"):
        for name in ("spp_if.exe", "spp_if"):
            p = os.path.join(ROOT, sub, name)
            if os.path.isfile(p):
                return p
    return shutil.which("spp_if")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binary or sample data is missing")
    args = ap.parse_args()

    exe = find_spp()
    if not exe:
        msg = "spp_if not built - run `gnss build` (or cmake --build build)"
        print(f"SKIP  {msg}")
        return 1 if args.required else 0

    sample = os.path.join(ROOT, "data", "sample")
    obs = os.path.join(sample, OBS)
    nav = os.path.join(sample, NAV)
    if not (os.path.isfile(obs) and os.path.isfile(nav)):
        msg = (f"sample data missing from {sample} - run "
               "`python scripts/make_sample_data.py` (see data/README.md)")
        print(f"SKIP  {msg}")
        return 1 if args.required else 0

    out = os.path.join(ROOT, "output", "_regression")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)

    cfg = os.path.join(ROOT, "config", "spp.ini")
    print(f"Running {os.path.basename(exe)} on the sample dataset...")
    r = subprocess.run([exe, cfg, "--obs", obs, "--nav", nav, "--out-dir", out],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:])
        print(r.stderr[-2000:], file=sys.stderr)
        check(False, f"solver exited {r.returncode}")
        return 1

    print("Comparing against tests/baseline/\n")
    pairs = [
        (os.path.join(out, OBS + "_DUAL_IF.spp.out"),
         os.path.join(ROOT, "tests", "baseline", "WUH2_20250101_DUAL_IF.spp.out")),
        (os.path.join(out, OBS + "_pos_vel.out"),
         os.path.join(ROOT, "tests", "baseline", "WUH2_20250101_pos_vel.out")),
    ]
    for got, want in pairs:
        name = os.path.basename(want)
        if not os.path.isfile(got):
            check(False, f"{name}: output not produced at {got}")
            continue
        with open(got, "rb") as f1, open(want, "rb") as f2:
            a, b = f1.read(), f2.read()
        if a == b:
            check(True, f"{name}: byte-identical ({len(a)} bytes)")
        else:
            ga, gb = a.splitlines(), b.splitlines()
            check(False, f"{name}: DIFFERS ({len(ga)} vs {len(gb)} lines)")
            for i, (x, y) in enumerate(zip(ga, gb)):
                if x != y:
                    print(f"        first difference at line {i + 1}:")
                    print(f"          got  {x.decode(errors='replace')}")
                    print(f"          want {y.decode(errors='replace')}")
                    break

    # The manifest is what the Python layer uses to find the output, so a run
    # that produces correct numbers but no manifest is still a broken pipeline.
    mf = os.path.join(out, OBS + "_manifest.json")
    check(os.path.isfile(mf), "manifest written")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("Regression passed: the sample reproduces the baseline exactly.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
