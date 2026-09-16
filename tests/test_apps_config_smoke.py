#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Smoke-test the four auxiliary programs' configuration plumbing.

`read_rinex`, `system_bias`, `bds_eph` and `bds_gps_diff` used to take no
command line at all and to carry absolute Windows paths compiled into them, so
they only ran on the machine they were written on. This checks the wiring that
replaced those paths: the config files are found and parsed, the command line
overrides them, a bad value is reported rather than absorbed, and nothing is
written into the repository root any more.

It is deliberately layered so that the cheap checks run everywhere and the
expensive ones skip:

  1. no data needed      --help and the shipped config files
  2. no data needed      config lookup, path resolution and error handling
  3. needs data/sample/  a real run of the two observation-based programs
  4. needs the SP3       a real run of the two ephemeris programs

Tier 3 self-skips when data/sample/ is absent, and tier 4 when the SP3 is
absent -- **even under --required**. The SP3 is not committed (see
data/README.md), so CI will never have it, and a --required run that demanded it
would fail for a missing download rather than a broken program.

    python tests/test_apps_config_smoke.py
    python tests/test_apps_config_smoke.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SAMPLE = os.path.join(ROOT, "data", "sample")
SAMPLE_OBS = os.path.join(SAMPLE, "WUH200CHN_R_20250010000_01D_30S_MO.rnx")
SAMPLE_NAV = os.path.join(SAMPLE, "BRDC00IGS_R_20250010000_01D_MN.rnx")
SP3 = os.path.join(ROOT, "data", "WUM0MGXFIN_20250010000_01D_05M_ORB.SP3")

SCRATCH = os.path.join(ROOT, "output", "_smoke")

# The names these four programs used to drop straight into the working
# directory. The `gnss app` CLI runs them with the repository root as the working
# directory, so this is what used to make a run dirty the checkout.
LEGACY_OUTPUTS = [
    "GNSS_Statistics.csv",
    "GPS_TGD_Result.csv",
    "BDS_TGD_Result.csv",
    "PL_IonoCompare.csv",
    "GPS_Iono.txt",
    "BDS_Iono.txt",
    "trop_delay_result.txt",
    "trop_zhd_zwd.txt",
    "tx_backward_verify.csv",
    "tx_if_single_diff.csv",
    "sat_pos_vel_diff_20250101.csv",
]

# program -> the config file it reads by default
PROGRAMS = {
    "read_rinex": "config/bias.ini",
    "system_bias": "config/bias.ini",
    "bds_eph": "config/eph.ini",
    "bds_gps_diff": "config/eph.ini",
}

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


def run(exe: str, args: list[str], cwd: str) -> subprocess.CompletedProcess:
    return subprocess.run([exe] + args, cwd=cwd, capture_output=True, text=True)


def tier1_help_and_configs(exes: dict[str, str]) -> None:
    print("Tier 1 - command line and shipped config files")

    for name, exe in exes.items():
        r = run(exe, ["--help"], ROOT)
        check(r.returncode == 0, f"{name} --help exits 0")
        check(PROGRAMS[name] in r.stdout,
              f"{name} --help names its config ({PROGRAMS[name]})")

    for cfg in ("config/bias.ini", "config/eph.ini"):
        path = os.path.join(ROOT, cfg)
        ok = os.path.isfile(path) and os.path.getsize(path) > 0
        check(ok, f"{cfg} exists and is not empty")

    # The exact key spellings the loaders look for. A rename here would silently
    # fall back to the built-in default, which is the failure mode this whole
    # change exists to make visible.
    expected = {
        "config/bias.ini": ["obsFile", "navFile", "outDir", "stopUTC"],
        "config/eph.ini": ["navFile", "sp3File", "outDir", "targetSat", "targetUTC",
                           "satList", "startUTC", "epochCount", "interval"],
    }
    for cfg, keys in expected.items():
        with open(os.path.join(ROOT, cfg), encoding="utf-8") as f:
            text = f.read()
        missing = [k for k in keys if f"{k} =" not in text and f"{k}=" not in text]
        check(not missing, f"{cfg} defines {len(keys)} expected keys"
                           + (f" (missing: {', '.join(missing)})" if missing else ""))


def tier2_plumbing(exes: dict[str, str], scratch: str) -> None:
    print("\nTier 2 - config lookup, path resolution and errors (no data)")

    exe = exes["read_rinex"]

    # A stand-in project laid out like the real one: <root>/config/<file>.ini.
    # The layout matters because the rule under test is that relative paths
    # resolve against the PARENT of the config's directory - the project root -
    # not the config's own directory and not the working directory.
    #
    # The scratch root is deliberately inside the repository rather than under
    # the system temp directory: std::filesystem here throws on a non-ASCII path
    # (a pre-existing defect, see docs/roadmap.md), and %TEMP% on this project's
    # development machine is under a Chinese user name.
    root = os.path.join(scratch, "plumbing")
    os.makedirs(os.path.join(root, "config"), exist_ok=True)
    cfg = os.path.join(root, "config", "plumbing.ini")
    with open(cfg, "w", encoding="utf-8") as f:
        f.write("obsFile = does-not-exist.rnx\n")
        f.write("navFile = does-not-exist.rnx\n")
        f.write("outDir = out\n")

    r = run(exe, [cfg], ROOT)
    check(r.returncode == 1, f"missing input exits 1 (got {r.returncode})")
    check("does-not-exist.rnx" in r.stderr,
          "the error names the resolved file")
    check(os.path.isdir(os.path.join(root, "out")),
          "outDir was resolved against the project root, not the working directory")
    check(not os.path.isdir(os.path.join(ROOT, "out")),
          "nothing was created under the working directory")

    # A malformed value is an error where it is actually parsed, rather than
    # being absorbed like a malformed optional setting.
    r = run(exe, [cfg, "--stop", "nonsense"], ROOT)
    check(r.returncode == 2, f"malformed stopUTC exits 2 (got {r.returncode})")
    check("malformed stopUTC" in r.stderr, "malformed stopUTC is named in the error")

    # A config named on the command line that does not exist is an error, not a
    # silent fallback to the built-in defaults.
    r = run(exe, [os.path.join(root, "config", "no-such.ini")], ROOT)
    check(r.returncode == 1 and "cannot open config file" in r.stderr,
          "a named config that does not exist is an error")

    # The default config name is searched upwards, so a run started from the
    # build tree - which is what an IDE does - still finds it.
    r = run(exe, [], os.path.join(ROOT, "tests"))
    check("searching upwards" in r.stderr and "bias.ini" in r.stderr,
          "the default config is found by searching upwards from a subdirectory")


def tier3_sample(exes: dict[str, str], scratch: str, required: bool) -> bool:
    print("\nTier 3 - the committed sample")

    if not (os.path.isfile(SAMPLE_OBS) and os.path.isfile(SAMPLE_NAV)):
        # data/sample/ IS committed, so unlike the SP3 this is a genuine
        # failure under --required - CI will always have it.
        msg = (f"sample data missing from {SAMPLE} - run "
               "`python scripts/make_sample_data.py` (see data/README.md)")
        if required:
            check(False, msg)
        else:
            print(f"SKIP  {msg}")
        return False

    out = os.path.join(scratch, "bias")
    common = ["--obs", SAMPLE_OBS, "--nav", SAMPLE_NAV, "--out-dir", out]

    r = run(exes["read_rinex"], common + ["--stop", "2025-01-01T00:01:00"], ROOT)
    check(r.returncode == 0, f"read_rinex exits 0 (got {r.returncode})")
    stats = os.path.join(out, "GNSS_Statistics.csv")
    if os.path.isfile(stats):
        with open(stats, encoding="utf-8") as f:
            lines = f.read().splitlines()
        check(lines and lines[0] == "Epoch,GPS,BDS,Galileo,GLONASS,Total",
              "GNSS_Statistics.csv has the documented header")
        check(len(lines) > 1, f"GNSS_Statistics.csv has data rows ({len(lines) - 1})")
    else:
        check(False, f"GNSS_Statistics.csv not written to {out}")

    r = run(exes["system_bias"], common + ["--stop", "2025-01-01T00:01:00"], ROOT)
    check(r.returncode == 0, f"system_bias exits 0 (got {r.returncode})")
    nine = ["GPS_TGD_Result.csv", "BDS_TGD_Result.csv", "PL_IonoCompare.csv",
            "GPS_Iono.txt", "BDS_Iono.txt", "trop_delay_result.txt",
            "trop_zhd_zwd.txt", "tx_backward_verify.csv", "tx_if_single_diff.csv"]
    missing = [n for n in nine if not os.path.isfile(os.path.join(out, n))]
    check(not missing, f"system_bias wrote all nine files"
                       + (f" (missing: {', '.join(missing)})" if missing else ""))
    empty = [n for n in ("GPS_TGD_Result.csv", "BDS_TGD_Result.csv", "PL_IonoCompare.csv")
             if os.path.isfile(os.path.join(out, n))
             and os.path.getsize(os.path.join(out, n)) == 0]
    check(not empty, f"the always-populated outputs are non-empty"
                     + (f" (empty: {', '.join(empty)})" if empty else ""))

    # The point of the whole change: whatever the defaults are, a run must not
    # scatter files into the repository root. This is the check that fails if
    # somebody drops an outDir prefix.
    stray = [n for n in LEGACY_OUTPUTS if os.path.isfile(os.path.join(ROOT, n))]
    check(not stray, "no output landed in the repository root"
                     + (f" (found: {', '.join(stray)})" if stray else ""))
    return True


def tier4_ephemeris(exes: dict[str, str], scratch: str) -> bool:
    print("\nTier 4 - the ephemeris programs (needs the SP3)")

    if not os.path.isfile(SP3):
        print(f"SKIP  {os.path.relpath(SP3, ROOT)} not present - it is not committed; "
              "see data/README.md")
        print("      (this skip stands even under --required, because CI never has it)")
        return False

    r = run(exes["bds_eph"],
            ["--nav", SAMPLE_NAV, "--sp3", SP3,
             "--sat", "C01", "--epoch", "2025-01-01T00:05:00"], ROOT)
    check(r.returncode == 0, f"bds_eph exits 0 (got {r.returncode})")
    check("Satellite: C01" in r.stdout, "bds_eph reports the requested satellite")
    # The clock model prints its own "elaptc:/af0:.../dtc:" block when it runs.
    # bds_eph used to print a frozen copy of that block as well - constants
    # captured from one run and pasted in, which then sat there looking like
    # results. Exactly one block must remain; two means the copy came back.
    #
    # Counting rather than matching the text, because the model's own values are
    # genuinely these for this satellite and epoch.
    n_clock = r.stdout.count("elaptc:")
    check(n_clock == 1, f"exactly one clock-model block on stdout (got {n_clock})")

    r = run(exes["bds_eph"], ["--nav", SAMPLE_NAV, "--sp3", SP3,
                              "--sat", "C05", "--epoch", "2025-01-01T00:10:00"], ROOT)
    check("Satellite: C05" in r.stdout, "bds_eph --sat is honoured")

    out = os.path.join(scratch, "eph")
    r = run(exes["bds_gps_diff"],
            ["--nav", SAMPLE_NAV, "--sp3", SP3, "--out-dir", out,
             "--sats", "C01", "--start", "2025-01-01T00:00:00",
             "--epochs", "4", "--interval", "30"], ROOT)
    check(r.returncode == 0, f"bds_gps_diff exits 0 (got {r.returncode})")
    # The name is derived from the start date, so pinning it here is what tests
    # that derivation.
    csv = os.path.join(out, "sat_pos_vel_diff_20250101.csv")
    if os.path.isfile(csv):
        with open(csv, encoding="utf-8") as f:
            lines = f.read().splitlines()
        check(len(lines) == 5, f"CSV has a header plus 4 rows (got {len(lines)})")
    else:
        check(False, f"sat_pos_vel_diff_20250101.csv not written to {out}")

    # A typo in the satellite list is reported, not silently skipped.
    r = run(exes["bds_gps_diff"], ["--sats", "C01,XX"], ROOT)
    check(r.returncode == 2, f"a bad satellite id exits 2 (got {r.returncode})")
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--required", action="store_true",
                    help="fail (not skip) when the binaries or the sample data are missing")
    args = ap.parse_args()

    exes: dict[str, str] = {}
    missing: list[str] = []
    for name in PROGRAMS:
        exe = find_exe(name)
        if exe:
            exes[name] = exe
        else:
            missing.append(name)

    if missing:
        msg = (f"not built: {', '.join(missing)} - run `gnss build` "
               "(or cmake --build build)")
        print(f"SKIP  {msg}")
        return 1 if args.required else 0

    scratch = SCRATCH
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch, exist_ok=True)

    tier1_help_and_configs(exes)
    tier2_plumbing(exes, scratch)
    tier3_sample(exes, scratch, args.required)
    tier4_ephemeris(exes, scratch)

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("Smoke test passed: the auxiliary programs read their configs and write "
          "to their output directories.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
