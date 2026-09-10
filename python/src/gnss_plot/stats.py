# -*- coding: utf-8 -*-
"""Accuracy statistics and the console report.

Two conventions are used throughout, and they answer different questions:

*internal sigma* (``std``)
    The standard deviation of the series about its own mean, with ``ddof=1``.
    This measures precision - how repeatable the solution is - and is unaffected
    by a constant offset.

*external RMS*
    Root-mean-square about the reference value. This includes any systematic
    offset, so it is always >= sigma.

For the static station the velocity truth is exactly zero, so both coincide.
For position, the reference is the RINEX header ``APPROX POSITION XYZ``, which
is itself only metre-to-decametre accurate, so the position RMS reported here is
``repeatability + reference offset`` rather than absolute accuracy. The README
states this explicitly.
"""

from __future__ import annotations

import numpy as np

from ._strings import t

__all__ = ["component_stats", "enu_stats", "report_spp_vel", "SPEED_OF_LIGHT"]

SPEED_OF_LIGHT = 2.99792458e8  # m/s, to convert clock drift to s/s


def component_stats(d):
    """One error series -> ``(bias, sigma, rms)``."""
    d = np.asarray(d, dtype=float)
    return float(d.mean()), float(d.std(ddof=1)), float(np.sqrt((d ** 2).mean()))


def enu_stats(d_e, d_n, d_u):
    """3-D ENU error series -> per-component and combined statistics.

    Returns a dict with ``bias``/``std``/``rms`` (3-element arrays, ordered
    E, N, U) plus scalars ``rms_h`` (horizontal) and ``rms_3d``.
    """
    rows = [component_stats(d) for d in (d_e, d_n, d_u)]
    d_e = np.asarray(d_e, dtype=float)
    d_n = np.asarray(d_n, dtype=float)
    d_u = np.asarray(d_u, dtype=float)
    return {
        "bias": np.array([r[0] for r in rows]),
        "std": np.array([r[1] for r in rows]),
        "rms": np.array([r[2] for r in rows]),
        "rms_h": float(np.sqrt((d_e ** 2 + d_n ** 2).mean())),
        "rms_3d": float(np.sqrt((d_e ** 2 + d_n ** 2 + d_u ** 2).mean())),
    }


def report_spp_vel(X, Y, Z, d_e, d_n, d_u, v_e, v_n, v_u, clkdot, ref_xyz,
                   station_label="", mode_label="DUAL_IF", lang="en"):
    """Print the accuracy report. Returns nothing."""
    n_p, n_v = np.size(d_e), np.size(v_e)
    pos = enu_stats(d_e, d_n, d_u)
    vel = enu_stats(v_e, v_n, v_u)

    if lang == "zh":
        names = ("东E ", "北N ", "天U ")
    else:
        names = ("E", "N", "U")

    width = 72
    print("=" * width)
    title = t(lang, "rpt_title", mode=mode_label)
    if station_label:
        title = f"{title}  ({station_label})"
    print(f"  {title}")
    print("=" * width)
    print("  " + t(lang, "rpt_epochs", np=n_p, nv=n_v))
    print("  " + t(lang, "rpt_ref", x=ref_xyz[0], y=ref_xyz[1], z=ref_xyz[2]))
    print("-" * width)

    print("  " + t(lang, "rpt_pos_head"))
    print(t(lang, "rpt_cols"))
    for nm, b, s, r in zip(names, pos["bias"], pos["std"], pos["rms"]):
        print(f"  {nm:<4s} {b:+10.3f}   {s:10.3f}   {r:10.3f}")
    print("  " + t(lang, "rpt_2d3d", h=pos["rms_h"], d=pos["rms_3d"]))
    print("  " + t(lang, "rpt_mean_xyz",
                  x=float(np.mean(X)), y=float(np.mean(Y)), z=float(np.mean(Z))))
    print("-" * width)

    print("  " + t(lang, "rpt_vel_head"))
    print(t(lang, "rpt_cols"))
    for nm, b, s, r in zip(names, vel["bias"], vel["std"], vel["rms"]):
        print(f"  {nm:<4s} {b:+10.5f}   {s:10.5f}   {r:10.5f}")
    print("  " + t(lang, "rpt_2d3d_vel", h=vel["rms_h"], d=vel["rms_3d"]))
    clkdot = np.asarray(clkdot, dtype=float)
    print("  " + t(lang, "rpt_clk",
                  lo=float(clkdot.min()), hi=float(clkdot.max()),
                  mean=float(clkdot.mean()),
                  secs=float(clkdot.mean()) / SPEED_OF_LIGHT))
    print("=" * width)
