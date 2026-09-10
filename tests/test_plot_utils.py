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
