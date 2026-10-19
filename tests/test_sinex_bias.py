#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The Bias-SINEX/OSB reader, checked without a downloaded product.

`apps/sinex_bias` is the command-line face of `SinexBiasStore`. This test writes
small synthetic `.BIA` fixtures and drives that program, so it covers the
reader's contract on a fresh clone and in CI:

  - the header fields that decide whether a file is usable at all
    (``%=BIA`` version, ``BIAS_MODE ABSOLUTE``, ``TIME_SYSTEM``);
  - fixed-width record parsing, including the optional station column;
  - unit conversion (``ns`` / ``s`` / ``m``) into metres;
  - half-open validity intervals, multiple intervals per (satellite, signal),
    and the ``86400`` end-of-day spelling;
  - what is deliberately skipped: DSB records, station-specific records, phase
    biases;
  - the sign: ``bias = observed - true``, so the corrected observation is the
    raw one **minus** the OSB. ``--raw`` pushes an observation through the same
    ``SPPIFCode::correctCodeBias`` hook the solver uses;
  - a satellite/signal the file does not cover is left uncorrected, not
    dropped, and the reader reports it as missing;
  - the ``--osb`` guard in ``rtk``: precise products are required.

The optional last tier runs against the real WHU file if it is in ``data/``
(gitignored) and checks that the C2I/C6I ionosphere-free combination of the
biases is zero - the property that ties the OSB set to its clock datum.

    python tests/test_sinex_bias.py
    python tests/test_sinex_bias.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

C_MPS = 299792458.0

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


def run(exe: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([exe, *args], capture_output=True, text=True, cwd=ROOT)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

def bias_record(typ="OSB", svn="C005", prn="C06", station="", obs1="C2I",
                obs2="", start="2022:062:00000", end="2022:062:86400",
                unit="ns", value="-10.0", std="0.0") -> str:
    """One fixed-width BIAS/SOLUTION line, columns as the IGS format defines.

    The products seen so far put: type at 1, SVN at 6, PRN at 11, STATION at 14,
    OBS1 at 25, OBS2 at 29, START at 35, END at 50, UNIT at 65, the value in the
    21-column field ending at 90, and the standard deviation ending at 102.
    """
    buf = [" "] * 104

    def put(i: int, s: str) -> None:
        for k, ch in enumerate(s):
            if 0 <= i + k < len(buf):
                buf[i + k] = ch

    put(1, typ)
    put(6, svn)
    put(11, prn)
    put(14, station)
    put(25, obs1)
    put(29, obs2)
    put(35, start)
    put(50, end)
    put(65, unit)
    put(70, "%21s" % value)
    put(92, "%11s" % std)
    return "".join(buf).rstrip()


def fixture_absolute() -> tuple[str, int]:
    """The main fixture; returns its text and its declared record count."""
    records = [
        # Code OSBs for C06: one per observable, C6I split into two intervals.
        bias_record(prn="C06", obs1="C2I", value="-10.0"),
        bias_record(prn="C06", obs1="C6I", value="-20.0",
                    end="2022:062:43200"),
        bias_record(prn="C06", obs1="C6I", value="-22.0",
                    start="2022:062:43200"),
        bias_record(prn="C06", obs1="C5P", unit="m", value="1.5"),
        bias_record(prn="C06", obs1="C7I", unit="s", value="1e-8"),
        # Phase: kept out of the code store and counted as skipped.
        bias_record(prn="C06", obs1="L2I", value="5.0"),
        # A second constellation's satellite, for the coverage report.
        bias_record(prn="C19", svn="C201", obs1="C2I", value="-30.0"),
        bias_record(prn="C19", svn="C201", obs1="C5P", value="-40.0"),
        # DSB: skipped.
        bias_record(typ="DSB", prn="C07", obs1="C2I", obs2="C6I", value="-1.0"),
        # Station-specific OSB: skipped.
        bias_record(prn="C08", station="TEST", obs1="C2I", value="3.0"),
        # End-of-day normalization and the half-open boundary.
        bias_record(prn="C09", obs1="C2I", value="-5.0"),
        bias_record(prn="C09", obs1="C2I", value="-6.0",
                    start="2022:063:00000", end="2022:063:86400"),
    ]
    count = len(records)
    text = "\n".join([
        f"%=BIA 1.00 TEST 2024:001:00000 WHU 2022:062:00000 2022:063:00000 A {count:08d}",
        "*" + "-" * 79,
        "+BIAS/DESCRIPTION",
        " BIAS_MODE                               ABSOLUTE",
        " TIME_SYSTEM                             G",
        " SATELLITE_CLOCK_REFERENCE_OBSERVABLES   C  C2I  C6I",
        "-BIAS/DESCRIPTION",
        "+BIAS/SOLUTION",
        "*BIAS SVN_ PRN STATION__ OBS1 OBS2 BIAS_START____ BIAS_END______ UNIT __ESTIMATED_VALUE____ _STD_DEV___",
        *records,
        "-BIAS/SOLUTION",
        "",
    ])
    return text, count


def fixture_relative() -> str:
    text, _ = fixture_absolute()
    return text.replace("BIAS_MODE                               ABSOLUTE",
                        "BIAS_MODE                               RELATIVE")


# ---------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------

def parse_bias_line(text: str, sat: str, obs: str) -> tuple[float, float] | None:
    """The (metres, ns) pair from one interval or query line for sat/obs.

    A --epoch query prints the interval dump first and the selected value on an
    ``at ...`` line afterwards, so prefer that line when it exists.
    """
    lines = text.splitlines()
    ordered = ([l for l in lines if l.strip().startswith("at ")] +
               [l for l in lines if not l.strip().startswith("at ")])
    for line in ordered:
        if f"{sat} {obs}" not in line:
            continue
        m = re.search(r"(-?\d+\.\d+) m \((-?\d+\.\d+) ns\)", line)
        if m:
            return float(m.group(1)), float(m.group(2))
    return None


def test_absolute(inspector: str, rtk: str, tmp: str) -> None:
    print("Absolute-mode fixture: summary, units, intervals, sign")
    text, count = fixture_absolute()
    path = os.path.join(tmp, "absolute.bia")
    with open(path, "w") as f:
        f.write(text)

    p = run(inspector, "--osb", path)
    check(p.returncode == 0, "absolute fixture loads")
    out = p.stdout
    check("bias mode     : ABSOLUTE" in out, "summary reports ABSOLUTE")
    check("time system   : G (GPS)" in out, "summary reports the GPS time system")
    check(f"header count  : {count} (matches)" in out,
          "the %=BIA record count matches what was parsed")
    check("code intervals 9" in out,
          "9 code intervals stored (11 OSB lines minus phase and station)")
    check("phase skipped 1" in out, "the phase OSB was skipped")
    check("1 DSB skipped" in out, "the DSB record was skipped")
    check("1 station-specific skipped" in out,
          "the station-specific record was skipped")
    check("satellites    : 3" in out, "three satellites carry code OSBs")
    check("  C : C2I C5P C6I C7I" in out,
          "code coverage lists exactly the C2I/C5P/C6I/C7I signals")

    # Unit conversions, one per spelling the format permits.
    p = run(inspector, "--osb", path, "--sat", "C06", "--obs", "C2I")
    got = parse_bias_line(p.stdout, "C06", "C2I")
    check(got is not None, "C06 C2I interval is printed")
    if got:
        m, ns = got
        check(abs(ns - (-10.0)) < 1e-9, "C06 C2I keeps its -10 ns value")
        check(abs(m - (-10.0e-9 * C_MPS)) < 1e-5,
              "ns is converted to metres with c = 299792458")

    p = run(inspector, "--osb", path, "--sat", "C06", "--obs", "C5P")
    got = parse_bias_line(p.stdout, "C06", "C5P")
    check(got is not None and abs(got[0] - 1.5) < 1e-12,
          "an 'm' unit is taken as metres")

    p = run(inspector, "--osb", path, "--sat", "C06", "--obs", "C7I")
    got = parse_bias_line(p.stdout, "C06", "C7I")
    check(got is not None and abs(got[0] - 1e-8 * C_MPS) < 1e-5,
          "an 's' unit is converted with c")

    # Interval selection: two C6I intervals, then the half-open end.
    for epoch, expect in (("2022:062:10000", -20.0),
                          ("2022:062:50000", -22.0),
                          ("2022:062:86399", -22.0)):
        p = run(inspector, "--osb", path, "--sat", "C06", "--obs", "C6I",
                "--epoch", epoch)
        got = parse_bias_line(p.stdout, "C06", "C6I")
        check(p.returncode == 0 and got is not None and abs(got[1] - expect) < 1e-9,
              f"C6I at {epoch} selects the {expect:g} ns interval")

    for epoch in ("2022:063:00000", "2022:061:86399"):
        p = run(inspector, "--osb", path, "--sat", "C06", "--obs", "C6I",
                "--epoch", epoch)
        check(p.returncode == 1 and "no OSB" in p.stdout,
              f"C6I at {epoch} is outside the half-open intervals")

    # The next day's C09 record starts exactly where the previous one ends.
    p = run(inspector, "--osb", path, "--sat", "C09", "--obs", "C2I",
            "--epoch", "2022:063:50000")
    got = parse_bias_line(p.stdout, "C09", "C2I")
    check(p.returncode == 0 and got is not None and abs(got[1] - (-6.0)) < 1e-9,
          "86400 normalizes to the next day, and the next day's interval is hit")

    # Sign and the end-to-end hook.
    p = run(inspector, "--osb", path, "--sat", "C06", "--obs", "C2I",
            "--epoch", "2022:062:10000", "--raw", "100.0")
    expect = 100.0 - (-10.0e-9 * C_MPS)
    m = re.search(r"corrected (-?\d+\.\d+) m", p.stdout)
    check(p.returncode == 0 and m is not None and
          abs(float(m.group(1)) - expect) < 1e-6,
          "corrected = observed - OSB (the SINEX-BIAS sign)")

    # Fail closed: C08 has only a station-specific record.
    p = run(inspector, "--osb", path, "--sat", "C08", "--obs", "C2I")
    check(p.returncode == 1 and "no OSB records" in p.stderr,
          "a satellite with only a station-specific record is reported missing")

    # rtk knows the option and refuses it without precise products.
    p = run(rtk, "--help")
    check("--osb" in p.stdout, "rtk --help documents --osb")
    p = run(rtk, "--osb", path)
    check(p.returncode == 2 and "requires both --sp3 and --clk" in p.stderr,
          "rtk rejects --osb without --sp3/--clk")


def test_error_paths(inspector: str, tmp: str) -> None:
    print("Error paths")
    relative = os.path.join(tmp, "relative.bia")
    with open(relative, "w") as f:
        f.write(fixture_relative())
    p = run(inspector, "--osb", relative)
    check(p.returncode == 1 and "ABSOLUTE" in p.stderr,
          "BIAS_MODE RELATIVE is rejected, naming ABSOLUTE")

    p = run(inspector, "--osb", os.path.join(tmp, "missing.bia"))
    check(p.returncode == 1 and "could not be opened" in p.stderr,
          "a missing file is reported as such")

    p = run(inspector, "--osb", relative + ".gz")
    check(p.returncode == 1 and "gunzip" in p.stderr,
          "a compressed file is rejected with a gunzip hint")


def test_real_product(inspector: str) -> None:
    """Optional: the C2I/C6I IF of the WHU biases must sum to zero."""
    real = os.path.join(ROOT, "data", "WUM0MGXRAP_20220620000_01D_01D_OSB.BIA")
    if not os.path.isfile(real):
        print("  SKIP  real WUM0MGXRAP OSB file not in data/")
        return
    print("Real WHU OSB product: the clock-reference IF closes to zero")

    def ns_at(obs: str) -> float | None:
        p = run(inspector, "--osb", real, "--sat", "C06", "--obs", obs,
                "--epoch", "2022:062:10000")
        if p.returncode != 0:
            return None
        m = re.search(r"\((-?\d+\.\d+) ns\)", p.stdout.splitlines()[-1])
        return float(m.group(1)) if m else None

    ns2, ns6 = ns_at("C2I"), ns_at("C6I")
    check(ns2 is not None and ns6 is not None,
          "the real file has C06 C2I and C6I at the test epoch")
    if ns2 is not None and ns6 is not None:
        f1, f2 = 1561.098e6, 1268.52e6
        a = f1 * f1 / (f1 * f1 - f2 * f2)
        b = -f2 * f2 / (f1 * f1 - f2 * f2)
        ifv = a * ns2 + b * ns6
        check(abs(ifv) < 0.01,
              f"IF(C2I,C6I) of the biases is {ifv:+.5f} ns (~0)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--required", action="store_true",
                        help="fail instead of skip when the build is missing")
    args = parser.parse_args()

    inspector = find_exe("sinex_bias")
    rtk = find_exe("rtk")
    if not inspector or not rtk:
        msg = "SKIP: build/bin/sinex_bias and build/bin/rtk are required"
        print(msg)
        return 1 if args.required else 0

    # Scratch under the repository (ASCII), not the OS %TEMP%: the developer's
    # user name is non-ASCII on Windows, and the C++ readers open paths as
    # narrow strings.
    with tempfile.TemporaryDirectory(dir=HERE) as tmp:
        test_absolute(inspector, rtk, tmp)
        test_error_paths(inspector, tmp)
    test_real_product(inspector)

    print()
    if failures:
        print(f"{len(failures)} failure(s)")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
