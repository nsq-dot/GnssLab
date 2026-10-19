#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BDS precise products with a matching OSB file, on the zero baseline.

This is the data-gated counterpart of test_sinex_bias.py. It needs the
gitignored zero-baseline pair and the WHU / WUM product set for 2022 DOY 062:

  data/WUM0MGXRAP_20220620000_01D_05M_ORB.SP3
  data/WUM0MGXRAP_20220620000_01D_30S_CLK.CLK
  data/WUM0MGXRAP_20220620000_01D_01D_OSB.BIA

What it pins is the reason the OSB reader exists: swapping in precise products
makes the BDS single-point columns worse because the AC's clock is referred to
C2I/C6I while the solver observes C2I/C7I (BDS-2) and C2I/C5P (BDS-3). With the
same center's bias file the columns must beat both the no-OSB precise run and
the broadcast baseline.

It also pins what must NOT change: the double differences cancel a satellite
bias, so the RTK float 3D RMS, the fix/no-fix decision on every epoch, and the
accepted fixed positions stay put (to a few centimetres - the OSB moves the SPP
linearization point, which is the only channel through which it can reach the
relative solution at all).

    python tests/test_rtk_osb.py
    python tests/test_rtk_osb.py --required   # fail instead of skip
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "python", "src"))

import numpy as np  # noqa: E402

from gnss_plot.rtk import (  # noqa: E402
    error_stats,
    load_rtk_diag,
    load_rtk_fixed,
    load_rtk_float,
)

TRUE_XYZ = (-2267803.4464, 5009342.4841, 3220991.0095)
ZERO = os.path.join(ROOT, "data", "Zero-baseline")
ROVER = os.path.join(ZERO, "oem719-202203031500-1.obs")
BASE = os.path.join(ZERO, "oem719-202203031500-2.obs")
NAV = os.path.join(ZERO, "BRDC00IGS_R_20220620000_01D_MN.rnx")
SP3 = os.path.join(ROOT, "data", "WUM0MGXRAP_20220620000_01D_05M_ORB.SP3")
CLK = os.path.join(ROOT, "data", "WUM0MGXRAP_20220620000_01D_30S_CLK.CLK")
OSB = os.path.join(ROOT, "data", "WUM0MGXRAP_20220620000_01D_01D_OSB.BIA")

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
    return None


def run_rtk(exe: str, mode: str, cfg: str, out: str) -> subprocess.CompletedProcess:
    args = [exe, os.path.join(ROOT, "config", "rtk.ini"),
            "--obs", ROVER, "--base-obs", BASE, "--nav", NAV,
            "--sys", mode, "--fix", "--trop",
            "--base-xyz", ",".join(str(v) for v in TRUE_XYZ),
            "--out-dir", out]
    if cfg in ("precise", "osb"):
        args += ["--sp3", SP3, "--clk", CLK]
    else:
        args += ["--bds-tgd"]
    if cfg == "osb":
        args += ["--osb", OSB]
    return subprocess.run(args, capture_output=True, text=True, cwd=ROOT)


def paths(out: str, mode: str) -> tuple[str, str, str]:
    base = os.path.join(out, os.path.basename(ROVER) + "_" + mode)
    return (base + "_rtk_float.out", base + "_rtk_diag.csv",
            base + "_rtk_fixed.out")


def results(flt: str, diag: str, fixed: str) -> dict:
    sod, spp, rtk = load_rtk_float(flt)
    stats = error_stats(sod, spp, rtk, TRUE_XYZ)
    _, dcols, _ = load_rtk_diag(diag)
    fixed_data = load_rtk_fixed(fixed)
    return {"sod": sod, "spp": spp, "rtk": rtk, "stats": stats,
            "diag": dcols, "fixed": fixed_data}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--required", action="store_true")
    args = parser.parse_args()

    exe = find_exe("rtk")
    needed = [ROVER, BASE, NAV, SP3, CLK, OSB]
    missing = [p for p in needed if not os.path.isfile(p)]
    if exe is None or missing:
        print("SKIP: needs build/bin/rtk, data/Zero-baseline/ and the WUM products")
        for p in missing:
            print("  missing:", p)
        return 1 if args.required else 0

    with tempfile.TemporaryDirectory(dir=HERE) as tmp:
        for mode in ("bds2", "bds3"):
            print(f"{mode}: broadcast / precise / precise+OSB")
            R = {}
            for cfg in ("bcast", "precise", "osb"):
                out = os.path.join(tmp, f"{mode}-{cfg}")
                p = run_rtk(exe, mode, cfg, out)
                check(p.returncode == 0,
                      f"{mode}-{cfg} run succeeds" +
                      ("" if p.returncode == 0 else f" ({p.stderr.strip()[:200]})"))
                if p.returncode != 0:
                    return 1
                flt, diag, fixed = paths(out, mode)
                R[cfg] = results(flt, diag, fixed)

            bcast = R["bcast"]["stats"]["spp"]["rms_3d"]
            prec = R["precise"]["stats"]["spp"]["rms_3d"]
            osb = R["osb"]["stats"]["spp"]["rms_3d"]
            print(f"    SPP 3D RMS: broadcast {bcast:.3f}  precise {prec:.3f}  "
                  f"+OSB {osb:.3f} m")
            check(osb < 0.8 * prec,
                  f"{mode}: OSB cuts the precise SPP error by more than 20%")
            check(osb <= bcast,
                  f"{mode}: +OSB is no worse than the broadcast baseline")

            # The double difference does not see the satellite bias.
            same = np.array_equal(R["precise"]["sod"], R["osb"]["sod"])
            check(same, f"{mode}: precise and OSB runs solve the same epochs")
            if same:
                d = np.abs(R["precise"]["rtk"] - R["osb"]["rtk"])
                check(d.max() < 0.05,
                      f"{mode}: RTK float moves by at most {d.max():.4f} m "
                      "(a changed linearization point, not the bias)")
                fa = R["precise"]["diag"].get("fixed")
                fb = R["osb"]["diag"].get("fixed")
                check(fa is not None and fb is not None and
                      np.array_equal(fa, fb),
                      f"{mode}: every epoch takes the same fix/no-fix decision")
                ra = R["precise"]["fixed"][3]
                rb = R["osb"]["fixed"][3]
                mask = (ra > 3.0) & (rb > 3.0)
                df = np.linalg.norm(
                    R["precise"]["fixed"][4][mask] - R["osb"]["fixed"][4][mask],
                    axis=1)
                check(df.max() < 0.06,
                      f"{mode}: accepted fixed positions move by at most "
                      f"{df.max():.4f} m")

        print("gps: precise / precise+OSB (control)")
        G = {}
        for cfg in ("precise", "osb"):
            out = os.path.join(tmp, f"gps-{cfg}")
            p = run_rtk(exe, "gps", cfg, out)
            check(p.returncode == 0, f"gps-{cfg} run succeeds")
            if p.returncode != 0:
                return 1
            G[cfg] = results(*paths(out, "gps"))
        gp = G["precise"]["stats"]["spp"]["rms_3d"]
        go = G["osb"]["stats"]["spp"]["rms_3d"]
        print(f"    SPP 3D RMS: precise {gp:.3f}  +OSB {go:.3f} m")
        check(go < gp, "gps: the OSB file does not degrade the GPS column")

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
