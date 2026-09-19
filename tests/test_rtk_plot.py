#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unit tests for the chapter-8 RTK accuracy reader and its figures.

Runs without a built C++ binary and without any data file, so it works on a
bare checkout: the fixture is a synthetic run written into a temporary
directory. That matters more here than for the SPP reader, because the real
input is the OEM719 zero-baseline dataset, which is a download rather than
something the repository carries.

    python -m pytest tests/ -q
    python tests/test_rtk_plot.py       # also runs standalone
"""

from __future__ import annotations

import hashlib
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                "python", "src"))

import numpy as np  # noqa: E402

from gnss_plot import cli, rtk  # noqa: E402

REF = np.asarray(rtk.BASE_HEADER_XYZ, dtype=float)
ROVER = "oem719-202203031500-1.obs"


# --------------------------------------------------------------------------
# fixture
# --------------------------------------------------------------------------

def _write_run(directory, mode="gps", rtk_offsets=((0.0, 0.0, 0.0),
                                                  (3.0, 4.0, 12.0)),
               spp_offsets=((3.0, 4.0, 12.0), (0.0, 0.0, 0.0)),
               sod0=24517.0, n_diag=2):
    """Write one synthetic run and return the directory it went into."""
    os.makedirs(directory, exist_ok=True)
    out = os.path.join(directory, "%s_%s%s" % (ROVER, mode, rtk.OUT_SUFFIX))
    with open(out, "w", encoding="utf-8") as f:
        for i, (s, r) in enumerate(zip(spp_offsets, rtk_offsets)):
            spp = REF + np.asarray(s, dtype=float)
            rtkp = REF + np.asarray(r, dtype=float)
            f.write("2022  62 %11.0f GPS spp: %.3f %.3f %.3f rtk: %.3f %.3f %.3f\n"
                    % (sod0 + i, spp[0], spp[1], spp[2], rtkp[0], rtkp[1], rtkp[2]))

    diag = os.path.join(directory, "%s_%s%s" % (ROVER, mode, rtk.DIAG_SUFFIX))
    with open(diag, "w", encoding="utf-8") as f:
        f.write("sod,nRoverEq,nSD,nDD,nUnk,rank,cond,datumSat,datumFallback,"
                "nSDsats,absDxyz,sigma0,postfitRms\n")
        for i in range(n_diag):
            f.write("%.6f,32,28,24,15,15,27.950182,G08,0,7,24.953518,"
                    "0.062900,0.023306\n" % (sod0 + i))

    manifest = os.path.join(directory, "%s_%s%s" % (ROVER, mode, rtk.MANIFEST_SUFFIX))
    with open(manifest, "w", encoding="utf-8") as f:
        f.write('{"sys": "%s", "sysLabel": "SYNTHETIC %s", "epochs": %d,'
                ' "cutOffElevation": 10}\n' % (mode, mode.upper(), len(rtk_offsets)))
    return directory


def _sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


# --------------------------------------------------------------------------
# readers
# --------------------------------------------------------------------------

def test_load_rtk_float_reads_both_columns():
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp)
        fn = os.path.join(tmp, "%s_gps%s" % (ROVER, rtk.OUT_SUFFIX))
        sod, spp, rtk_xyz = rtk.load_rtk_float(fn)

    assert list(sod) == [24517.0, 24518.0]
    assert spp.shape == (2, 3) and rtk_xyz.shape == (2, 3)
    assert abs(rtk_xyz[1][2] - (REF[2] + 12.0)) < 1e-3
    assert abs(spp[0][0] - (REF[0] + 3.0)) < 1e-3


def test_load_rtk_float_ignores_the_other_printSolution_overload():
    """`float-rtk: ... ratio: ... fixed-rtk:` is a different column layout.

    Reading it positionally would produce a full table of plausible, wrong
    numbers, so the two label tokens are checked and a non-matching row is
    skipped rather than parsed.
    """
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "r.out")
        with open(fn, "w", encoding="utf-8") as f:
            f.write("2022  62       24517 GPS spp: -1.0 2.0 3.0 "
                    "float-rtk: -1.1 2.1 3.1 ratio:2.5 fixed-rtk: -1.2 2.2 3.2\n")
        sod, spp, rtk_xyz = rtk.load_rtk_float(fn)

    assert sod.size == 0 and spp.size == 0 and rtk_xyz.size == 0


def test_load_rtk_diag_reads_named_columns_and_the_datum_token():
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp)
        fn = os.path.join(tmp, "%s_gps%s" % (ROVER, rtk.DIAG_SUFFIX))
        sod, cols, datum = rtk.load_rtk_diag(fn)

    assert list(sod) == [24517.0, 24518.0]
    assert list(datum) == ["G08", "G08"]
    assert cols["rank"][0] == 15.0
    assert abs(cols["postfitRms"][0] - 0.023306) < 1e-9
    assert "sod" not in cols, "sod comes back separately, not twice"


def test_load_rtk_diag_survives_a_missing_column():
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "d.csv")
        with open(fn, "w", encoding="utf-8") as f:
            f.write("sod,rank\n1.0,4\n2.0,5\n")
        sod, cols, _datum = rtk.load_rtk_diag(fn)

    assert list(sod) == [1.0, 2.0]
    assert sorted(cols) == ["rank"]


def test_load_rtk_diag_without_sod_yields_nothing():
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "d.csv")
        with open(fn, "w", encoding="utf-8") as f:
            f.write("rank,cond\n4,1.0\n")
        sod, cols, datum = rtk.load_rtk_diag(fn)

    assert sod.size == 0 and cols == {} and datum.size == 0


# --------------------------------------------------------------------------
# statistics
# --------------------------------------------------------------------------

def test_error_stats_are_rotation_invariant_but_the_split_is_not():
    """One run of two epochs: a zero offset and a (3, 4, 12) m one.

    Every quantity with a known value is checked exactly. The offset is 13 m
    long and lands on one of the two epochs, so each 3-D RMS is 13/sqrt(2) and
    the ratio is 1 - which is the check that the ratio really is
    reference-independent here.
    """
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp)
        fn = os.path.join(tmp, "%s_gps%s" % (ROVER, rtk.OUT_SUFFIX))
        sod, spp, rtk_xyz = rtk.load_rtk_float(fn)
    s = rtk.error_stats(sod, spp, rtk_xyz, REF)

    # The fixture writes three decimals, as the C++ does, so the "exact" values
    # below carry a millimetre of rounding each - the tolerance is that, not
    # float noise.
    TOL = 5e-3

    assert s["n"] == 2
    assert abs(s["rtk"]["rms_3d"] - 13.0 / np.sqrt(2.0)) < TOL
    assert abs(s["spp"]["rms_3d"] - 13.0 / np.sqrt(2.0)) < TOL
    assert abs(s["ratio"] - 1.0) < TOL

    # The mean is half the vector and the RMS about it is 13/sqrt(2). In ENU
    # the split between axes differs from ECEF, but the trace of the covariance
    # is rotation-invariant, so the two square-sum to the same number.
    assert abs(np.linalg.norm(s["rtk"]["bias"]) - 6.5) < TOL
    assert abs(np.linalg.norm(s["rtk"]["std"]) - 6.5 * np.sqrt(2.0)) < TOL
    assert abs(np.sum(s["rtk"]["std"] ** 2)
               - np.sum(s["ecef"]["rtk"]["sigma"] ** 2)) < TOL
    # ECEF is where the offset was written, so its axes are direct: the two
    # samples are 0 and 3 m apart, whose sample sigma is 3/sqrt(2).
    assert abs(s["ecef"]["rtk"]["sigma"][0] - 3.0 / np.sqrt(2.0)) < TOL
    assert abs(s["ecef"]["rtk"]["sigma"][2] - 12.0 / np.sqrt(2.0)) < TOL

    # |d| runs 0 to 13, so the median is 6.5 and the max is 13.
    assert abs(s["percentile"]["p50"] - 6.5) < TOL
    assert abs(s["percentile"]["max"] - 13.0) < TOL


def test_error_stats_on_an_empty_run_does_not_raise():
    s = rtk.error_stats(np.array([]), np.zeros((0, 3)), np.zeros((0, 3)), REF)
    assert s["n"] == 0
    assert np.isnan(s["percentile"]["p50"])


def test_comparison_table_carries_the_ratio_in_the_headline():
    """The one reference-independent number has to be where a reader sees it."""
    with tempfile.TemporaryDirectory() as tmp:
        _build = _write_run(tmp)                        # noqa: F841
        results = [rtk.load_run(rtk.find_runs(tmp)[0], REF)]
    table = rtk.comparison_table(results, "en")

    assert "SPP/RTK 3-D RMS improvement" in table
    assert "1.0x" in table, table
    # Bias and sigma are separate rows, and the frame is named.
    assert "RTK bias" in table and "RTK sigma" in table
    assert "ECEF" in table and "ENU" in table


def test_comparison_table_is_translated():
    """The Chinese table must build too - placeholders and all."""
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp)
        results = [rtk.load_run(rtk.find_runs(tmp)[0], REF)]
    table = rtk.comparison_table(results, "zh")

    assert "改善比" in table
    assert "{ratio}" not in table


# --------------------------------------------------------------------------
# run discovery
# --------------------------------------------------------------------------

def test_find_runs_parses_the_name_from_the_right():
    """The rover basename may itself contain underscores and dots."""
    with tempfile.TemporaryDirectory() as tmp:
        nested = os.path.join(tmp, "full")
        _write_run(nested, mode="bds2")
        found = rtk.find_runs(tmp)

    assert len(found) == 1
    assert found[0]["rover"] == ROVER
    assert found[0]["mode"] == "bds2"
    assert found[0]["diag"].endswith("_bds2" + rtk.DIAG_SUFFIX)


def test_select_runs_prefers_the_longer_run():
    """A 25-epoch smoke run beside the full one must not win."""
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp, mode="gps", n_diag=2)
        _write_run(os.path.join(tmp, "full"), mode="gps",
                   rtk_offsets=tuple((1.0, 0.0, 0.0) for _ in range(9)),
                   spp_offsets=tuple((1.0, 0.0, 0.0) for _ in range(9)))
        chosen = rtk.select_runs(rtk.find_runs(tmp))

    assert len(chosen) == 1
    assert "full" in chosen[0]["out"]


def test_select_runs_orders_by_the_documented_mode_order():
    with tempfile.TemporaryDirectory() as tmp:
        for mode in ("bds3", "gps", "bds2"):
            _write_run(tmp, mode=mode)
        chosen = rtk.select_runs(rtk.find_runs(tmp))

    assert [r["mode"] for r in chosen] == ["gps", "bds2", "bds3"]


# --------------------------------------------------------------------------
# the command, end to end
# --------------------------------------------------------------------------

def test_rtk_plot_writes_two_figures_and_they_are_deterministic():
    """Byte-for-byte reproducibility, which chapter 7 was bitten by.

    The class of bug is an unordered container - a `set` of satellite ids -
    iterated before plotting, which reorders the artists and changes the PNG
    bytes without changing the picture. Generating twice and comparing hashes
    is the only reliable test for it.
    """
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp, mode="gps")
        pngs = []
        for run in ("a", "b"):
            png_dir = os.path.join(tmp, "figs-" + run)
            # `--opt=value`: an ECEF X can start with a minus, and argparse
            # would otherwise read the value as another option.
            rc = cli.main(["rtk-plot", "--out-dir", tmp, "--png-dir", png_dir,
                           "--lang", "en",
                           "--ref-xyz=%.4f,%.4f,%.4f" % tuple(REF)])
            assert rc == 0
            got = sorted(os.listdir(png_dir))
            assert got == ["vis_rtk_accuracy_bars.png", "vis_rtk_error_enu_ts.png"], got
            pngs.append([_sha(os.path.join(png_dir, n)) for n in got])

    assert pngs[0] == pngs[1], "the same input must produce the same bytes"


def test_rtk_plot_fails_when_there_is_nothing_to_plot():
    with tempfile.TemporaryDirectory() as tmp:
        empty = os.path.join(tmp, "empty")
        os.makedirs(empty)
        assert cli.main(["rtk-plot", "--out-dir", empty,
                         "--png-dir", os.path.join(tmp, "figs")]) != 0


def test_rtk_plot_accepts_explicit_output_paths():
    with tempfile.TemporaryDirectory() as tmp:
        _write_run(tmp, mode="gps")
        out = os.path.join(tmp, "%s_gps%s" % (ROVER, rtk.OUT_SUFFIX))
        png_dir = os.path.join(tmp, "figs")
        rc = cli.main(["rtk-plot", "--rtk-out", out, "--png-dir", png_dir,
                       "--no-figures"])

    assert rc == 0


# --------------------------------------------------------------------------
# runner (so the file works without pytest)
# --------------------------------------------------------------------------

def _main() -> int:
    tests = [(n, f) for n, f in sorted(globals().items())
             if n.startswith("test_") and callable(f)]
    failed = []
    for name, fn in tests:
        try:
            fn()
            print(f"  ok    {name}")
        except Exception as e:  # noqa: BLE001
            failed.append((name, e))
            print(f"  FAIL  {name}: {type(e).__name__}: {e}")
    print(f"\n{len(tests) - len(failed)}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(_main())
