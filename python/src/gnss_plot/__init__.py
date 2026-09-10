# -*- coding: utf-8 -*-
"""gnss_plot - accuracy analysis and plotting for the GnssLab C++ engine.

Reads the text output written by ``apps/spp_if`` and produces accuracy
statistics, a console report, and figures.

The public API is deliberately flat so a short script can do useful work::

    import gnss_plot as gp

    ref = gp.read_approx_position("data/WUH200CHN_....rnx")
    sod, X, Y, Z = gp.load_spp_xyz("output/....spp.out")
    dE, dN, dU = gp.enu_position_error(X, Y, Z, ref)

    gp.figures.configure(lang="en", backend="Agg")
    gp.fig_pos_horizontal(dE, dN, "horizontal.png")

Nothing is configured at import time - call :func:`gnss_plot.figures.configure`
before plotting, or use the ``gnss`` command-line interface which does it for you.
"""

from __future__ import annotations

__version__ = "1.0.0"

from .coords import ecef_delta_to_enu, enu_position_error, xyz2blh
from .io import (
    find_manifest,
    load_pos_vel,
    load_spp_xyz,
    output_paths,
    read_approx_position,
    read_manifest,
)
from .stats import component_stats, enu_stats, report_spp_vel

__all__ = [
    "__version__",
    # io
    "load_spp_xyz", "load_pos_vel", "read_approx_position",
    "read_manifest", "find_manifest", "output_paths",
    # coords
    "xyz2blh", "ecef_delta_to_enu", "enu_position_error",
    # stats
    "component_stats", "enu_stats", "report_spp_vel",
    # submodules
    "figures",
]


def __getattr__(name):
    """Import `figures` lazily.

    ``figures`` pulls in matplotlib, which takes a noticeable moment to import.
    Deferring it keeps ``gnss_plot`` usable for a stats-only task without paying
    for a plotting stack.

    Uses importlib rather than ``from . import figures``: the latter resolves the
    name through getattr(), which lands back here and recurses until the
    interpreter gives up.
    """
    if name == "figures":
        import importlib
        module = importlib.import_module(".figures", __name__)
        # Cache it, so later accesses bypass this hook entirely.
        globals()["figures"] = module
        return module
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
