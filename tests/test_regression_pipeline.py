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


def normalise_eol(data: bytes) -> bytes:
    """Drop the CR of CRLF line endings.

    The solver writes its output in text mode, so the C runtime turns "\\n"
    into "\\r\\n" on Windows and leaves it alone everywhere else. The baseline
    is a Windows-generated file and stays CRLF in the repository, so the raw
    bytes differ by platform while every number inside them is the same. Only
    the terminator is normalised here - the rest is still compared byte for
    byte, so a changed digit, a changed field width or a changed field order
    all still fail.
    """
    return data.replace(b"\r\n", b"\n")


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
            raw_got, raw_want = f1.read(), f2.read()
        a, b = normalise_eol(raw_got), normalise_eol(raw_want)
        if a == b:
            where = "CRLF" if b"\r\n" in raw_want else "LF"
            check(True, f"{name}: byte-identical modulo line endings "
                        f"({len(a)} bytes, baseline is {where})")
            continue
        ga, gb = a.splitlines(), b.splitlines()
        check(False, f"{name}: DIFFERS ({len(ga)} vs {len(gb)} lines)")
        differing = [(i, x, y) for i, (x, y) in enumerate(zip(ga, gb)) if x != y]
        if differing:
            i, x, y = differing[0]
            print(f"        first difference at line {i + 1}:")
            print(f"          got  {x.decode(errors='replace')}")
            print(f"          want {y.decode(errors='replace')}")
        else:
            # Every line matches once the terminators are normalised, so the
            # two files differ in trailing whitespace or in whether the last
            # line carries a terminator at all.
            print("        every line matches after normalising line endings; "
                  "the files differ in trailing whitespace or in the final "
                  "newline")

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
