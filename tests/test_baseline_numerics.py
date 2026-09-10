#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Check the frozen solver output in tests/baseline/ against known values.

This needs neither a compiler nor the full dataset - only the two files in
tests/baseline/, which are committed. It guards two things:

  1. That the baseline files themselves have not been altered (SHA-256), since
     every other regression check is measured against them.
  2. That they still describe the same solution (the statistics the README
     quotes), which catches a baseline that is intact but was frozen from the
     wrong run.

Registered with CTest as `baseline_numerics`.

    python tests/test_baseline_numerics.py
"""

from __future__ import annotations

import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "python", "src"))

import numpy as np  # noqa: E402

from gnss_plot import enu_position_error, load_pos_vel, load_spp_xyz, read_approx_position  # noqa: E402

BASELINE = os.path.join(HERE, "baseline")
SPP = os.path.join(BASELINE, "WUH2_20250101_DUAL_IF.spp.out")
VEL = os.path.join(BASELINE, "WUH2_20250101_pos_vel.out")

# Frozen hashes. If these change, the regression reference has changed and
# every dependent check needs re-baselining deliberately, not by accident.
SHA_SPP = "092efbfd74e4d9f64e3aeb254f1ffc49bd1983a20849d73dc9f99bf7f42e8c61"
SHA_VEL = "61a19f2bc0233c9dc4d0b83765f51aa251ac1db17d48f5286ed1031467233bfe"

# Frozen statistics, reproduced from the baseline during the restructure.
# sigma is ddof=1; RMS is about the reference; velocity truth is 0.
EXPECT_VEL = {
    "vE": (-0.008061, 0.005828, 0.009920),
    "vN": (+0.001240, 0.005055, 0.005165),
    "vU": (-0.002883, 0.017616, 0.017712),
}
EXPECT_CLK = (1.249573, 1.982838, 1.608308)  # min, max, mean
EXPECT_EPOCHS = 63

# The reference used for position, taken from the RINEX header. Hardcoded here
# so this test does not need the (69 MB) observation file.
REF_XYZ = np.array([-2267749.0, 5009154.0, 3221290.0])


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check(cond: bool, msg: str, failures: list) -> None:
    if cond:
        print(f"  ok    {msg}")
    else:
        print(f"  FAIL  {msg}")
        failures.append(msg)


def main() -> int:
    failures: list[str] = []

    print("Baseline integrity")
    for path, want, label in ((SPP, SHA_SPP, "spp.out"), (VEL, SHA_VEL, "pos_vel.out")):
        if not os.path.isfile(path):
            check(False, f"{label} present", failures)
            continue
        got = sha256(path)
        check(got == want, f"{label} sha256 {'matches' if got == want else f'IS {got[:16]}..., expected {want[:16]}...'}",
              failures)

    if failures:
        print("\nBaseline files are missing or altered; further checks skipped.")
        return 1

    print("\nParsing")
    sod_p, X, Y, Z = load_spp_xyz(SPP)
    sod_v, _x, _y, _z, v_e, v_n, v_u, clk = load_pos_vel(VEL)
    check(sod_p.size == EXPECT_EPOCHS, f"position epochs = {sod_p.size} (expected {EXPECT_EPOCHS})", failures)
    check(sod_v.size == EXPECT_EPOCHS, f"velocity epochs = {sod_v.size} (expected {EXPECT_EPOCHS})", failures)
    check(abs(sod_v[0]) < 1e-9, "first epoch at sod 0", failures)
    check(abs(sod_v[-1] - 1860.0) < 1e-6, f"last epoch at sod {sod_v[-1]:.1f} (expected 1860)", failures)

    print("\nVelocity statistics")
    for name, series in (("vE", v_e), ("vN", v_n), ("vU", v_u)):
        bias, sigma, rms = series.mean(), series.std(ddof=1), float(np.sqrt((series ** 2).mean()))
        want_b, want_s, want_r = EXPECT_VEL[name]
        ok = (abs(bias - want_b) < 5e-7 and abs(sigma - want_s) < 5e-7
              and abs(rms - want_r) < 5e-7)
        check(ok, f"{name} bias {bias:+.6f} (want {want_b:+.6f}), "
                  f"sigma {sigma:.6f} (want {want_s:.6f}), rms {rms:.6f} (want {want_r:.6f})",
              failures)

    print("\nClock drift")
    lo, hi, mean = clk.min(), clk.max(), clk.mean()
    check(abs(lo - EXPECT_CLK[0]) < 5e-7 and abs(hi - EXPECT_CLK[1]) < 5e-7
          and abs(mean - EXPECT_CLK[2]) < 5e-7,
          f"range {lo:.6f}-{hi:.6f} mean {mean:.6f} "
          f"(want {EXPECT_CLK[0]:.6f}-{EXPECT_CLK[1]:.6f} mean {EXPECT_CLK[2]:.6f})",
          failures)

    print("\nCross-file consistency")
    # The two outputs carry the same positions at different precision, so they
    # should agree to within the 3-decimal rounding of the .spp.out. A larger
    # disagreement would mean they came from different runs.
    n = min(X.size, _x.size)
    d = max(float(np.abs(X[:n] - _x[:n]).max()),
            float(np.abs(Y[:n] - _y[:n]).max()),
            float(np.abs(Z[:n] - _z[:n]).max()))
    check(d <= 5e-4 + 1e-9,
          f"max |dXYZ| between the two outputs = {d:.6f} m (rounding half-step 0.0005)",
          failures)

    print("\nPosition statistics")
    d_e, d_n, d_u = enu_position_error(X, Y, Z, REF_XYZ)
    # Loose bounds: these depend on the reference coordinate, which is only
    # decametre-accurate, so they exist to catch a gross error (a wrong column,
    # a swapped axis), not to pin the value.
    check(abs(d_e.mean() - 2.20) < 1.0, f"E bias {d_e.mean():+.3f} m", failures)
    check(abs(d_u.mean() - 11.42) < 2.0, f"U bias {d_u.mean():+.3f} m", failures)
    check(d_e.std(ddof=1) < 3.0 and d_u.std(ddof=1) < 6.0,
          f"internal sigma E {d_e.std(ddof=1):.3f} m, U {d_u.std(ddof=1):.3f} m", failures)

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("All baseline checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
