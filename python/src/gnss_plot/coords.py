# -*- coding: utf-8 -*-
"""Coordinate conversions: ECEF <-> geodetic, and ECEF deltas -> local ENU."""

from __future__ import annotations

import numpy as np

__all__ = ["xyz2blh", "ecef_delta_to_enu", "enu_position_error", "WGS84_A", "WGS84_F"]

# WGS84 ellipsoid.
WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563


def xyz2blh(x, y, z):
    """ECEF metres -> ``(lat, lon, h)`` with the angles in radians.

    Bowring's iteration, applied uniformly so scalars and arrays both work. Eight
    iterations is well past convergence for terrestrial heights; the loop is kept
    fixed-length rather than convergence-tested because that keeps the function
    vectorised and branch-free.
    """
    a, f = WGS84_A, WGS84_F
    e2 = 2 * f - f * f

    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    z = np.asarray(z, dtype=float)

    lon = np.arctan2(y, x)
    p = np.hypot(x, y)
    lat = np.arctan2(z, p * (1 - e2))

    h = np.zeros_like(lat)
    for _ in range(8):
        n = a / np.sqrt(1 - e2 * np.sin(lat) ** 2)
        h = p / np.cos(lat) - n
        lat = np.arctan2(z, p * (1 - e2 * n / (n + h)))

    return lat, lon, h


def ecef_delta_to_enu(dx, dy, dz, lat0, lon0):
    """Rotate ECEF difference vectors into the local ENU frame at (lat0, lon0).

    Returns ``(E, N, U)``. Inputs may be scalars or arrays; array inputs are
    stacked as columns, so N epochs give N-element outputs.
    """
    slat, clat = np.sin(lat0), np.cos(lat0)
    slon, clon = np.sin(lon0), np.cos(lon0)

    rot = np.array([
        [-slon, clon, 0.0],
        [-slat * clon, -slat * slon, clat],
        [clat * clon, clat * slon, slat],
    ])

    d = np.stack([np.asarray(dx, dtype=float).ravel(),
                  np.asarray(dy, dtype=float).ravel(),
                  np.asarray(dz, dtype=float).ravel()])  # 3 x N
    e, n, u = rot @ d
    return e, n, u


def enu_position_error(X, Y, Z, ref_xyz):
    """Local ENU error of a position series against a reference ECEF coordinate."""
    lat0, lon0, _ = xyz2blh(ref_xyz[0], ref_xyz[1], ref_xyz[2])
    return ecef_delta_to_enu(
        np.asarray(X) - ref_xyz[0],
        np.asarray(Y) - ref_xyz[1],
        np.asarray(Z) - ref_xyz[2],
        lat0, lon0,
    )
