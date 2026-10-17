#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Checks for the mixed GPS + BeiDou solution (`rtk --sys gpsbds`).

What is asserted, and why each one is worth a test:

* The orbital-family classifier reads the broadcast elements. C01 must be GEO,
  C06 IGSO, C11 MEO; C59 is the trap - it is GEO even though its PRN sits above
  the IGSO block.
* A mixed epoch carries one reference satellite per constellation. If the code
  kept the old global datum, every BeiDou satellite would be silently dropped
  by differenceSat() and nDD would collapse to the GPS-only count.
* The BeiDou quota never exceeds the cap and honours a zero (excluded) family.
* The Kalman path adds one ambiguity constraint per reference satellite; with
  two references the rank must still be full and every ambiguity must carry a
  cycle-slip flag.

Needs `rtk` built and data/Zero-baseline/ present (~95 MB per file, gitignored),
so this does not run in CI:

    python tests/test_rtk_mixed.py
    python tests/test_rtk_mixed.py --required   # fail instead of skip
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
OBS = "oem719-202203031500-1.obs"
TRUE_XYZ = "-2267803.4464,5009342.4841,3220991.0095"
STOP = "2022-03-03T06:49:00"       # the same 25-epoch window the other RTK tests use

failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(f"  {'ok  ' if cond else 'FAIL'}  {msg}")
    if not cond:
        failures.append(msg)


def find_exe(name: str) -> str | None:
    for sub in ("build/bin", "build-debug/bin", "cmake-build-debug/bin",
                "cmake-build-debug", "bin"):
        for suffix in (".exe", ""):
            p = os.path.join(ROOT, sub, name + suffix)
            if os.path.isfile(p):
                return p
    return shutil.which(name)


def run(exe: str, args: list[str], expect: int = 0) -> subprocess.CompletedProcess:
    proc = subprocess.run([exe, os.path.join(ROOT, "config", "rtk.ini")] + args,
                          cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != expect:
        print(proc.stdout[-2000:])
        print(proc.stderr[-2000:])
    check(proc.returncode == expect,
          f"rtk {' '.join(args)} exits {expect} (got {proc.returncode})")
    return proc


def read_diag(path: str) -> list[dict]:
    with open(path, "r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def read_manifest(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def finite_solution(path: str) -> int:
    n = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            fields = line.split()
            if len(fields) < 12:
                continue
            vals = [float(v) for v in fields[5:8] + fields[9:12]]
            if not all(math.isfinite(v) for v in vals):
                return -1
            n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail when the binary or the data are missing")
    args = ap.parse_args()

    exe = find_exe("rtk")
    have_data = all(os.path.isfile(os.path.join(ZERO, n)) for n in (
        OBS,
        "oem719-202203031500-2.obs",
        "BRDC00IGS_R_20220620000_01D_MN.rnx",
    ))
    if exe is None or not have_data:
        msg = "rtk binary or data/Zero-baseline/ is missing"
        if args.required:
            check(False, msg)
            return 1
        print(f"SKIP: {msg}")
        return 0

    base = os.path.join(ROOT, "output", "_rtk_mixed")
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base)

    print("Orbital-family classification (broadcast elements)")
    proc = subprocess.run([exe, os.path.join(ROOT, "config", "rtk.ini"),
                           "--dump-orbit-types"], cwd=ROOT,
                          capture_output=True, text=True)
    check(proc.returncode == 0, "--dump-orbit-types exits 0")
    kinds = {}
    for line in proc.stdout.splitlines()[1:]:
        parts = line.strip().split(",")
        if len(parts) == 2:
            kinds[parts[0]] = parts[1]
    for sat, want in (("C01", "GEO"), ("C06", "IGSO"), ("C11", "MEO"),
                      ("C38", "IGSO"), ("C59", "GEO")):
        check(kinds.get(sat) == want, f"{sat} is classified {want} (got {kinds.get(sat)})")

    common = ["--fix", "--trop", "--bds-tgd", "--base-xyz", TRUE_XYZ, "--stop", STOP]

    print("Mixed solution: two references, more equations than either system alone")
    out_mix = os.path.join(base, "mix")
    run(exe, ["--sys", "gpsbds", "--out-dir", out_mix] + common)
    mix_diag = read_diag(os.path.join(out_mix, OBS + "_gpsbds_rtk_diag.csv"))
    check(len(mix_diag) == 25, f"25 epochs solved (got {len(mix_diag)})")
    check(all(";" in row["datumSats"] and "G" in row["datumSats"]
              and "C" in row["datumSats"] for row in mix_diag),
          "every epoch has a GPS and a BeiDou reference")
    check(all(int(row["nDD"]) > 0 for row in mix_diag),
          "every epoch has double differences")
    check(all(int(row["bdsSelGEO"]) + int(row["bdsSelIGSO"]) + int(row["bdsSelMEO"]) > 0
              for row in mix_diag),
          "every epoch carries BeiDou satellites")

    out_gps = os.path.join(base, "gps")
    run(exe, ["--sys", "gps", "--out-dir", out_gps] + common)
    gps_diag = read_diag(os.path.join(out_gps, OBS + "_gps_rtk_diag.csv"))
    check(int(mix_diag[0]["nDD"]) > int(gps_diag[0]["nDD"]),
          "mixed nDD exceeds GPS-only nDD at the first epoch "
          f"({mix_diag[0]['nDD']} vs {gps_diag[0]['nDD']})")
    check(int(mix_diag[0]["nSDsats"]) > int(gps_diag[0]["nSDsats"]),
          "mixed satellite count exceeds GPS-only")
    check(finite_solution(os.path.join(
        out_mix, OBS + "_gpsbds_rtk_float.out")) == 25,
        "mixed float solution is finite for all 25 epochs")

    manifest = read_manifest(os.path.join(out_mix, OBS + "_gpsbds_manifest.json"))
    check(manifest.get("sys") == "gpsbds", "manifest records sys=gpsbds")
    check(set(manifest.get("systems", [])) == {"G", "C"},
          "manifest records both constellations")
    check(manifest.get("bdsMaxSats") == 0, "manifest records the default (no cap)")

    print("Quota: a zero family is excluded and the cap holds")
    out_q = os.path.join(base, "quota")
    run(exe, ["--sys", "gpsbds", "--bds-max-sats", "4",
              "--bds-orbit-ratio", "0,1,1", "--out-dir", out_q] + common)
    q_diag = read_diag(os.path.join(out_q, OBS + "_gpsbds_rtk_diag.csv"))
    check(all(int(row["bdsSelGEO"]) == 0 for row in q_diag),
          "GEO quota of zero excludes every GEO")
    check(all(0 < int(row["bdsSelGEO"]) + int(row["bdsSelIGSO"]) +
              int(row["bdsSelMEO"]) <= 4 for row in q_diag),
          "selected BeiDou satellites stay within the cap")
    check(all("G" in row["datumSats"] and "C" in row["datumSats"] for row in q_diag),
          "the capped solution still has both references")

    print("Exercise 3's covariance path also survives the two-system block")
    out_c = os.path.join(base, "ddcov")
    run(exe, ["--sys", "gpsbds", "--dd-cov", "--out-dir", out_c] + common)
    check(finite_solution(os.path.join(
        out_c, OBS + "_gpsbds_rtk_float.out")) == 25,
        "--dd-cov mixed float solution is finite for all 25 epochs")

    print("Kalman: one ambiguity constraint per reference")
    out_k = os.path.join(base, "kalman")
    run(exe, ["--sys", "gpsbds", "--estimator", "kalman", "--dump-cs",
              "--out-dir", out_k] + common)
    k_manifest = read_manifest(os.path.join(out_k, OBS + "_gpsbds_manifest.json"))
    check(k_manifest.get("csAmbiguitiesWithoutFlag") == 0,
          "every Kalman ambiguity has a cycle-slip flag "
          f"(missing: {k_manifest.get('csAmbiguitiesWithoutFlag')})")
    k_diag = read_diag(os.path.join(out_k, OBS + "_gpsbds_rtk_diag.csv"))
    check(all(int(row["rank"]) == int(row["nUnk"]) for row in k_diag),
          "the two-reference Kalman system is full rank")

    print("Malformed quota")
    run(exe, ["--sys", "gpsbds", "--bds-orbit-ratio", "0,0,0",
              "--out-dir", os.path.join(base, "bad")], expect=2)

    if failures:
        print(f"\n{len(failures)} check(s) failed")
        return 1
    print("\nMixed GPS + BeiDou checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
