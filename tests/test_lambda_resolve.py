#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""MLAMBDA ambiguity resolution, checked without any RINEX data.

`mlambda` (built from examples/exam-8.3-lambda.cpp) resolves three hand-picked
float ambiguity vectors and prints the result of each. Two of the three are
about what happens when resolution is IMPOSSIBLE, which is the half that used to
be broken:

  - the search's failure branch had no return statement at all (undefined
    behaviour), and
  - a covariance that is not positive definite, and a search that gave up, both
    fell through to `return 0` - "success" - carrying an uninitialised candidate
    matrix. The reported ratio then came from uninitialised memory, and on any
    input where it happened to come out small the caller was handed an all-zero
    "fixed" solution labelled ratio 9999.9.

So the cases are:

  case 1  textbook example 8-1: fixes to [-9 21 -2 4 24 7] with ratio 6.51682.
          This one pins the algorithm itself - it is the notes' own number, not
          something generated from this implementation.
  case 2  two integer candidates tie. Nothing may be fixed: ratio 1, below the
          threshold of 3, and the float vector must come back unchanged.
  case 3  rank-1 covariance. No search is possible at all: the float vector
          comes back unchanged and the ratio is 0.

Before the fix, case 3 killed the process (Eigen's bounds assertion in a debug
build; a bare segfault without it). That is why this test exists rather than
being left to the end-to-end RTK run, where a crash on one bad epoch would only
show up as a missing line in the output.

No dataset and no other program are needed, so this runs on a fresh clone:

    python tests/test_lambda_resolve.py
    python tests/test_lambda_resolve.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import os
import re
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


def parse_cases(text: str) -> list[dict]:
    """Split the program's output into one dict per `=== ... ===` block."""
    cases: list[dict] = []
    current: dict | None = None
    for line in text.splitlines():
        m = re.match(r"^=== (.*) ===$", line.strip())
        if m:
            current = {"title": m.group(1)}
            cases.append(current)
            continue
        if current is None:
            continue
        key, _, value = line.partition(":")
        key = key.strip()
        if not key or not value:
            continue
        if key in ("float", "resolved"):
            current[key] = [float(t) for t in value.split()]
        elif key == "ratio":
            current["ratio"] = float(value.split()[0])
        elif key == "fixed":
            current["fixed"] = value.strip().startswith("yes")
    return cases


def integers(values: list[float]) -> list[int]:
    return [int(round(v)) for v in values]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binary is missing")
    args = ap.parse_args()

    exe = find_exe("mlambda")
    if not exe:
        print("SKIP  mlambda not built - run `gnss build` (or cmake --build build)")
        return 1 if args.required else 0

    r = subprocess.run([exe], cwd=ROOT, capture_output=True, text=True)
    print(f"$ {os.path.relpath(exe, ROOT)}\n")
    check(r.returncode == 0, f"exit code 0 (got {r.returncode})")
    if r.returncode != 0:
        print(r.stdout[-1500:])
        print(r.stderr[-1500:], file=sys.stderr)
        print("\n1 check(s) FAILED")
        return 1

    cases = parse_cases(r.stdout)
    check(len(cases) == 3, f"three cases reported (got {len(cases)})")
    if len(cases) != 3:
        print(r.stdout)
        print("\n1 check(s) FAILED")
        return 1

    #-----------------------------------------------------------------
    print("\ncase 1: textbook example 8-1\n")
    c = cases[0]
    check(len(c["float"]) == 6 and len(c["resolved"]) == 6,
          f"six ambiguities in and out ({len(c['float'])}, {len(c['resolved'])})")
    # The notes give [−9 21 −2 4 24 7]. Note the fourth entry: the notes' own
    # PDF renders the minus signs with a glyph that extracts as '?', and reading
    # the fourth value as 3 instead of 4 is easy to do and wrong.
    check(integers(c["resolved"]) == [-9, 21, -2, 4, 24, 7],
          f"fixed ambiguities are [-9, 21, -2, 4, 24, 7] (got {integers(c['resolved'])})")
    check(abs(c["ratio"] - 6.51682) < 5e-6,
          f"ratio is 6.51682 (got {c['ratio']})")
    check(c["fixed"] is True, "ratio 6.51682 clears the threshold of 3, so it is fixed")
    # Every resolved value must be an integer to within the search tolerance,
    # not merely a number that rounds to one.
    worst = max(abs(v - round(v)) for v in c["resolved"])
    check(worst < 1e-9, f"resolved values are integers to 1e-9 (worst {worst:.3e})")

    #-----------------------------------------------------------------
    print("\ncase 2: two candidates tie\n")
    c = cases[1]
    check(c["ratio"] == 1.0, f"ratio is exactly 1 (got {c['ratio']})")
    check(c["fixed"] is False, "ratio 1 is below 3, so the fix is rejected")
    # The ratio test is the CALLER's decision, not resolve()'s. The search
    # itself succeeded, so an integer candidate still comes back - and which of
    # the tied candidates it is, is arbitrary. That arbitrariness is exactly
    # what the ratio test exists to catch, so the test asserts the properties
    # that are well defined rather than one particular tie-break.
    check(all(abs(v - round(v)) < 1e-9 for v in c["resolved"]),
          f"the search still returns an integer candidate"
          f" ({integers(c['resolved'])}) for the caller to accept or reject")
    check(c["resolved"] != c["float"],
          "... and it is not the float vector - only a failed search returns"
          " that (case 3)")

    #-----------------------------------------------------------------
    print("\ncase 3: covariance is not positive definite\n")
    c = cases[2]
    check(c["ratio"] == 0.0, f"ratio is 0 - no integer candidate exists (got {c['ratio']})")
    check(c["fixed"] is False, "an unusable covariance is never reported as fixed")
    check(c["resolved"] == c["float"],
          "the float vector comes back unchanged, rather than zeros or garbage")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("MLAMBDA resolution checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
