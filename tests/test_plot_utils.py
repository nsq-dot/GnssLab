#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unit tests for the gnss_plot coordinate, statistics and parsing helpers.

These run without a built C++ binary and without any data file, so they work on
a bare checkout.

    python -m pytest tests/ -q
    python tests/test_plot_utils.py     # also runs standalone
"""

from __future__ import annotations

import math
import os
import string
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                "python", "src"))

import numpy as np  # noqa: E402

from gnss_plot import coords, stats  # noqa: E402
from gnss_plot._strings import normalise_lang, t  # noqa: E402

TOL = 1e-9


# --------------------------------------------------------------------------
# coords
# --------------------------------------------------------------------------

def test_xyz2blh_equator():
    """A point on the equator at h=0 has lat 0, lon per its quadrant."""
    lat, lon, h = coords.xyz2blh(coords.WGS84_A, 0.0, 0.0)
    assert abs(lat) < 1e-9
    assert abs(lon) < 1e-9
    assert abs(h) < 1e-6


def test_xyz2blh_pole():
    """The north pole: lat = +90 deg, and h is measured from the semi-minor axis."""
    b = coords.WGS84_A * (1 - coords.WGS84_F)
    lat, _lon, h = coords.xyz2blh(0.0, 0.0, b)
    assert abs(math.degrees(lat) - 90.0) < 1e-6
    assert abs(h) < 1e-6


def test_xyz2blh_matches_known_wuhan():
    """WUH2's RINEX header position round-trips through the BLH conversion.

    Latitude/longitude are cross-checked against the published station
    coordinates; the height is only checked for plausibility, since the header
    position is itself decametre-accurate.
    """
    lat, lon, h = coords.xyz2blh(-2267749.0, 5009154.0, 3221290.0)
    assert abs(math.degrees(lat) - 30.53) < 0.02
    assert abs(math.degrees(lon) - 114.36) < 0.02
    assert 0 < h < 200


def test_xyz2blh_vectorised_matches_scalar():
    """The array path and the scalar path must agree."""
    xs = np.array([-2267749.0, 4000000.0, 0.0])
    ys = np.array([5009154.0, 3000000.0, 6378137.0])
    zs = np.array([3221290.0, 2000000.0, 0.0])
    lat_v, lon_v, h_v = coords.xyz2blh(xs, ys, zs)
    for i in range(3):
        lat_s, lon_s, h_s = coords.xyz2blh(xs[i], ys[i], zs[i])
        assert abs(lat_v[i] - lat_s) < TOL
        assert abs(lon_v[i] - lon_s) < TOL
        assert abs(h_v[i] - h_s) < 1e-6


def test_ecef_delta_to_enu_pure_components():
    """An ECEF offset along each local axis must land on the matching ENU axis.

    Constructs the offsets by rotating the unit ENU vectors back into ECEF, so
    this checks the rotation rather than restating it.
    """
    lat0, lon0 = math.radians(30.53), math.radians(114.36)
    slat, clat = math.sin(lat0), math.cos(lat0)
    slon, clon = math.sin(lon0), math.cos(lon0)
    # Rows of the ECEF->ENU rotation, used here as columns of its transpose.
    rows = np.array([
        [-slon, clon, 0.0],
        [-slat * clon, -slat * slon, clat],
        [clat * clon, clat * slon, slat],
    ])
    for k in range(3):
        dx, dy, dz = rows[k]  # a unit vector along local axis k
        e, n, u = coords.ecef_delta_to_enu(dx, dy, dz, lat0, lon0)
        got = np.array([e[0], n[0], u[0]])
        want = np.eye(3)[k]
        assert np.allclose(got, want, atol=1e-12), (k, got, want)


def test_enu_position_error_zero_for_reference():
    ref = np.array([-2267749.0, 5009154.0, 3221290.0])
    e, n, u = coords.enu_position_error(ref[0], ref[1], ref[2], ref)
    assert abs(e) < 1e-9 and abs(n) < 1e-9 and abs(u) < 1e-9


def test_enu_position_error_magnitude_preserved():
    """The rotation is orthonormal, so an ECEF offset keeps its length."""
    ref = np.array([-2267749.0, 5009154.0, 3221290.0])
    dx, dy, dz = 3.0, -4.0, 12.0
    e, n, u = coords.enu_position_error(ref[0] + dx, ref[1] + dy, ref[2] + dz, ref)
    assert abs(math.sqrt(e[0] ** 2 + n[0] ** 2 + u[0] ** 2) - 13.0) < 1e-9


# --------------------------------------------------------------------------
# stats
# --------------------------------------------------------------------------

def test_component_stats_known_values():
    d = np.array([1.0, 2.0, 3.0, 4.0, 5.0])
    bias, sigma, rms = stats.component_stats(d)
    assert abs(bias - 3.0) < TOL
    assert abs(sigma - np.std(d, ddof=1)) < TOL
    assert abs(rms - math.sqrt((d ** 2).mean())) < TOL


def test_component_stats_empty_is_nan_not_crash():
    bias, sigma, rms = stats.component_stats(np.array([]))
    assert math.isnan(bias) and math.isnan(sigma) and math.isnan(rms)


def test_rms_includes_bias_sigma_does_not():
    """The two conventions measure different things, which is the point."""
    d = np.array([9.8, 10.0, 10.2])  # mean 10, tiny spread
    bias, sigma, _ = stats.component_stats(d)
    assert abs(bias - 10.0) < TOL
    assert sigma < 0.3  # precision is good
    assert math.sqrt((d ** 2).mean()) > 9.0  # accuracy is not


def test_enu_stats_combines_components():
    d_e = np.array([1.0, -1.0])
    d_n = np.array([2.0, -2.0])
    d_u = np.array([0.0, 0.0])
    s = stats.enu_stats(d_e, d_n, d_u)
    assert s["bias"].shape == (3,)
    assert abs(s["bias"][0]) < TOL and abs(s["bias"][1]) < TOL
    assert abs(s["rms_h"] - math.sqrt(5.0)) < 1e-9
    assert abs(s["rms_3d"] - math.sqrt(5.0)) < 1e-9


def test_rms_relations():
    rng = np.random.default_rng(0)
    d = rng.normal(0.5, 2.0, 500)
    s = stats.enu_stats(d, d, d)
    # Combined RMS is sqrt(3) times the per-component RMS when the components match.
    assert abs(s["rms_3d"] - math.sqrt(3) * s["rms"][0]) < 1e-9
    assert abs(s["rms_h"] - math.sqrt(2) * s["rms"][0]) < 1e-9


# --------------------------------------------------------------------------
# strings
# --------------------------------------------------------------------------

def test_normalise_lang():
    assert normalise_lang("zh") == "zh"
    assert normalise_lang("zh-CN") == "zh"
    assert normalise_lang("ZH_cn") == "zh"
    assert normalise_lang("en") == "en"
    assert normalise_lang("en-GB") == "en"
    assert normalise_lang("fr") == "en"   # unsupported falls back
    assert normalise_lang(None) == "en"
    assert normalise_lang("") == "en"


def test_every_key_translated_in_both_languages():
    from gnss_plot._strings import STRINGS
    en, zh = set(STRINGS["en"]), set(STRINGS["zh"])
    assert en == zh, f"missing zh: {en - zh}; extra zh: {zh - en}"


def test_placeholder_sets_match_across_languages():
    """A format string must use the same placeholders in both languages.

    A mismatch here produces a KeyError at plot time, on one language only.
    """
    from gnss_plot._strings import STRINGS

    def fields_of(s):
        return {f for _, f, _, _ in string.Formatter().parse(s) if f is not None}

    for key in STRINGS["en"]:
        assert fields_of(STRINGS["en"][key]) == fields_of(STRINGS["zh"][key]), key


def test_no_unexpected_braces_in_strings():
    """Guard against LaTeX or other literal braces in a format string.

    A bare '{' makes str.format() raise KeyError or ValueError. This is easy to
    introduce when adding a scientific label and only shows up when that label
    is finally formatted.
    """
    from gnss_plot._strings import STRINGS

    for lang, table in STRINGS.items():
        for key, value in table.items():
            try:
                value.format(**{f: 1 for f in
                                (x for _, x, _, _ in string.Formatter().parse(value) if x)})
            except (KeyError, IndexError, ValueError) as e:
                raise AssertionError(f"{lang}/{key} is not a valid format string: {e}")


def test_t_formats_and_falls_back():
    assert "E" in t("en", "lbl_east")
    assert t("en", "rpt_2d3d", h=1.0, d=2.0)
    assert t("zh", "rpt_2d3d", h=1.0, d=2.0)
    assert t("en", "no_such_key") == "no_such_key"


# --------------------------------------------------------------------------
# cycle-slip detector output
# --------------------------------------------------------------------------
# Synthetic files rather than the real ones: output/cs/ is not committed, and the
# point of these tests is the parser, not the data. tempfile rather than pytest's
# tmp_path because this file must also run under its own _main().

def test_load_gf_summary_reads_total_and_metadata():
    """The TOTAL row is short, has empty fields, and the footer holds commas."""
    from gnss_plot import io

    text = (
        "sat,nEpochs,nTested,nSlip,nOk,nInit,nGap,nWarmup,slipRate\n"
        "C01,100,99,9,90,1,0,0,0.090909\n"
        "C02,50,50,0,50,0,0,0,0.000000\n"
        "TOTAL,150,149,9,,,,\n"                     # shorter AND emptier
        "# mode,diff\n"                             # plain key/value
        "# note,MW does not separate arc-start / data-gap / slip;\n"   # commas!
        "# gfPolyWindow,30\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "summary.gf.diff.csv")
        with open(fn, "w", encoding="utf-8") as f:
            f.write(text)
        sat, cols, meta, total = io.load_gf_summary(fn)

    assert list(sat) == ["C01", "C02"], "TOTAL must not appear as a satellite"
    assert list(cols["nSlip"]) == [9.0, 0.0]
    assert list(cols["slipRate"]) == [0.090909, 0.0]
    assert total == {"nEpochs": 150.0, "nTested": 149.0, "nSlip": 9.0}, total
    assert meta["gfPolyWindow"] == "30"
    assert meta["note"] == "MW does not separate arc-start / data-gap / slip;", \
        "a footer value containing a comma must survive intact"


def test_load_gf_detector_takes_schema_from_the_comment():
    """Column names come from the file's own '#' header, and nan is a value."""
    from gnss_plot import io

    text = (
        "# sat year doy sod timeSystem LI_m dLI_m flag status\n"
        "# flag 1 = slip, 2 = arc start; untested epochs carry nan.\n"
        "C01 2025 1 0 GPS 5.148473 nan 2 INIT\n"
        "C01 2025 1 30 GPS 5.150000 0.001527 0 OK\n"
        "C01 2025 1 60 GPS 5.400000 0.250000 1 SLIP\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "C01.gf.diff")
        with open(fn, "w", encoding="utf-8") as f:
            f.write(text)
        sat, cols, status = io.load_gf_detector(fn)

    assert sat == "C01"
    assert list(status) == ["INIT", "OK", "SLIP"]
    assert list(cols["sod"]) == [0.0, 30.0, 60.0]
    assert list(cols["flag"]) == [2.0, 0.0, 1.0], "flag stays numeric"
    assert np.isnan(cols["dLI_m"][0]), "the literal nan token parses to nan"
    assert cols["dLI_m"][2] == 0.25
    # The comment lines contain a comma and a semicolon; they must not be read as
    # data. Three rows in, three rows out.
    assert cols["sod"].size == 3


def test_load_slip_manifest_keeps_arrays_parallel():
    from gnss_plot import io

    text = (
        "sat,year,doy,sod,label,dN1,dN2,expected_dLI_m,expect\n"
        "G01,2022,62,24517.000,arc-start,5,5,-0.269583,INIT\n"
        "G01,2022,62,25650.000,min-cycle-pair,1,1,-0.053917,SLIP\n"
        "G27,2022,62,26827,truncated,77\n"          # short row: skipped
    )
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "truth.slips.csv")
        with open(fn, "w", encoding="utf-8") as f:
            f.write(text)
        sat, year, doy, sod, label, dn1, dn2, edli, expect = io.load_slip_manifest(fn)

    assert list(sat) == ["G01", "G01"], "the short row is dropped, not half-read"
    assert list(expect) == ["INIT", "SLIP"], "expect stays a token, not a number"
    assert list(dn1) == [5.0, 1.0]
    assert sod.size == year.size == label.size == dn2.size == 2


def test_load_slip_manifest_survives_a_missing_column():
    """A file that does not have the expected columns yields empty arrays."""
    from gnss_plot import io

    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "bad.slips.csv")
        with open(fn, "w", encoding="utf-8") as f:
            f.write("sat,sod,label\nG01,1.0,arc-start\n")
        _sat, _y, _d, sod, _l, _a, _b, _c, _e = io.load_slip_manifest(fn)

    assert sod.size == 0


def _manifest(sat, year, doy, sod, label, dn1, dn2, edli, expect):
    return (np.asarray(sat, dtype=str), np.asarray(year, dtype=float),
            np.asarray(doy, dtype=float), np.asarray(sod, dtype=float),
            np.asarray(label, dtype=str), np.asarray(dn1, dtype=float),
            np.asarray(dn2, dtype=float), np.asarray(edli, dtype=float),
            np.asarray(expect, dtype=str))


def test_score_injection_counts_every_outcome():
    """One hit, one miss, one absent epoch, one adversarial right and wrong.

    This is the semantics the chapter-7 document quotes as 7/8 and 6/8, so it is
    pinned here with no data file involved.
    """
    from gnss_plot import cycleslip

    truth = _manifest(
        sat=["G01", "G01", "G02", "G07", "G27"],
        year=[2022] * 5, doy=[62] * 5,
        sod=[100.0, 200.0, 300.0, 400.0, 500.0],
        label=["hit", "missed", "absent", "arc-start", "wrong-arc"],
        dn1=[1, 1, 1, 5, 5], dn2=[1, 1, 1, 5, 5],
        edli=[-0.0539, -0.0539, -0.0539, -0.2696, -0.2696],
        expect=["SLIP", "SLIP", "SLIP", "INIT", "INIT"],
    )
    run = {
        ("G01", 2022, 62, 100.0): "SLIP",    # tp
        ("G01", 2022, 62, 200.0): "OK",      # fn - judged, but no slip
        # G02 @ 300.0 absent entirely       # fn - status MISSING
        ("G07", 2022, 62, 400.0): "INIT",    # adversarial, correct
        ("G27", 2022, 62, 500.0): "SLIP",    # adversarial, wrong
        ("G30", 2022, 62, 600.0): "SLIP",    # not injected: an extra detection
    }
    s = cycleslip.score_injection(truth, run)

    assert s["tp"] == 1
    assert s["fn"] == 2, "both a judged-but-quiet epoch and an absent one are misses"
    assert abs(s["rate"] - 1 / 3) < 1e-12
    assert s["adversarial_ok"] == 1
    assert s["adversarial_bad"] == 1
    assert s["extra_detections"] == 1
    assert s["tested"] == 4, "OK + SLIP only; INIT is not a judgement"

    by_label = {c["label"]: c for c in s["cases"]}
    assert by_label["absent"]["status"] == "MISSING"
    assert by_label["hit"]["verdict"] == "detected"
    assert by_label["missed"]["verdict"] == "MISSED"
    assert by_label["wrong-arc"]["verdict"] == "WRONG (expected INIT)"
    assert by_label["arc-start"]["detected"] is None, \
        "an adversarial case is not a detection question"


def test_score_injection_with_no_injected_slips():
    """rate is None, not a ZeroDivisionError, when nothing expects a slip."""
    from gnss_plot import cycleslip

    truth = _manifest(sat=["G01"], year=[2022], doy=[62], sod=[100.0],
                      label=["arc-start"], dn1=[5], dn2=[5], edli=[-0.2696],
                      expect=["INIT"])
    s = cycleslip.score_injection(truth, {("G01", 2022, 62, 100.0): "INIT"})
    assert s["rate"] is None
    assert s["adversarial_ok"] == 1


def test_series_view_absolutises_the_statistic():
    """Both detectors go on one log axis, so the statistic has to be unsigned."""
    from gnss_plot import cycleslip

    text = (
        "# sat year doy sod timeSystem LI_m dLI_m meanDL_m sigmaDL_m flag status\n"
        "C01 2025 1 0 GPS 5.0 -0.25 0.0 0.0 1 SLIP\n"
        "C01 2025 1 30 GPS 5.1 0.50 0.0 0.0 0 OK\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "C01.gf.diff")
        with open(fn, "w", encoding="utf-8") as f:
            f.write(text)
        sod, li, stat, flag, status = cycleslip.series_view(fn, "diff")

    assert list(sod) == [0.0, 30.0]
    assert list(li) == [5.0, 5.1]
    assert list(stat) == [0.25, 0.50], "signed dLI must come back positive"
    assert list(flag) == [1.0, 0.0]
    assert list(status) == ["SLIP", "OK"]


def test_mw_wavelength_matches_the_cpp_constants():
    """The wavelength the MW figures divide by.

    Pinned by value because these frequencies are copied from `getFreq()` in
    src/Const.h rather than read from it, and a drift here would rescale every
    MW figure without looking wrong. BeiDou is the pair to watch: the detectors
    use B1I/B2b (bands L2/L7), while Const.h's `L1_FREQ_BDS` is B1C at
    1575.42 MHz, which no cycle-slip path ever selects.
    """
    from gnss_plot import cycleslip

    assert abs(cycleslip.mw_wavelength("G08") - 0.861918) < 1e-6
    assert abs(cycleslip.mw_wavelength("C10") - 0.846972) < 1e-6
    # The satellite id is only a carrier for the system letter.
    assert cycleslip.mw_wavelength("G24") == cycleslip.mw_wavelength("G01")

    try:
        cycleslip.mw_wavelength("R05")
    except ValueError:
        pass
    else:
        raise AssertionError("an unmodelled system must raise, not guess a scale")


def test_series_view_returns_mw_in_cycles():
    """The `.mw` file holds metres; the figures are drawn in wide-lane cycles.

    The fixture repeats the shape the real files have: at the flagged epoch the
    stored mean equals that epoch's own combination, because the detector resets
    the mean the moment it flags and the mean is written after the update. The
    bias therefore has to be taken against the PREVIOUS epoch's mean - against
    the same epoch's it is identically zero at exactly the epochs that matter,
    which is what this pins.

    The two GF kinds are deliberately not converted - the sibling test above
    pins their `li` at the metres the file holds.
    """
    from gnss_plot import cycleslip

    text = (
        "# sat year doy sod timeSystem mw_m meanMW_m csFlagArg flag\n"
        "G08 2022 62 0 GPS 100.0 100.0 0 0\n"
        "G08 2022 62 1 GPS 169.0 169.0 1 1\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        fn = os.path.join(tmp, "G08.mw")
        with open(fn, "w", encoding="utf-8") as f:
            f.write(text)
        sod, li, stat, _flag, status = cycleslip.series_view(fn, "mw")

    # 69 m between these two epochs is the ~80 cycle step figure 7-1 annotates.
    assert abs((li[1] - li[0]) - 80.0) < 0.1, li
    # Recovered from the previous epoch's mean, so the reset mean stored at the
    # flagged epoch does not zero it out.
    assert abs(stat[1] - 69.0 / 0.861918) < 0.1, stat
    # The first epoch has no mean carried in, so it has no bias.
    assert math.isnan(stat[0]), stat
    assert list(sod) == [0.0, 1.0]
    assert list(status) == ["OK", "SLIP"]


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
