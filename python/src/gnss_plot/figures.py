# -*- coding: utf-8 -*-
"""Figure generation.

Every ``fig_*`` function follows the same contract: data arrays in, a PNG path
out, and the path returned.

Matplotlib is *not* configured at import time. An earlier revision called
``matplotlib.use("Agg")`` and set a Windows-only font at module import, which
made the module unusable from a notebook or an interactive session and silently
overrode a user's own backend choice. Call :func:`configure` explicitly instead
(the CLI does this) or rely on the defaults.
"""

from __future__ import annotations

import os

import numpy as np
import matplotlib
import matplotlib.pyplot as plt

from ._strings import t
from .stats import component_stats

__all__ = [
    "configure",
    "C_E", "C_N", "C_U", "INK", "GRIDC", "ZERO",
    "fig_pos_error_enu_ts",
    "fig_pos_horizontal",
    "fig_velocity_enu_ts",
    "fig_clock_drift",
]

# Colour slots (blue / orange / aqua), plus ink, grid and zero-line greys.
# These are a colour-blind-safe categorical set that stays legible in both light
# and dark rendering.
C_E, C_N, C_U = "#2a78d6", "#eb6834", "#1baf7a"
INK, GRIDC, ZERO = "#1a1a1a", "#c9c9c9", "#888888"

# Fonts that carry CJK glyphs, best first. SimHei ships with Windows, the
# Noto/WenQuanYi families with most Linux distributions, so probing beats
# hardcoding - otherwise every Chinese label renders as an empty box off Windows.
_CJK_FONTS = [
    "Microsoft YaHei",
    "SimHei",
    "Noto Sans CJK SC",
    "Noto Sans SC",
    "Source Han Sans SC",
    "WenQuanYi Zen Hei",
    "WenQuanYi Micro Hei",
    "PingFang SC",
    "Heiti SC",
]

_configured = False
_font_warned = False


def _available_fonts() -> set[str]:
    try:
        from matplotlib import font_manager
        return {f.name for f in font_manager.fontManager.ttflist}
    except Exception:  # pragma: no cover - font backend unavailable
        return set()


def configure(lang: str = "en", backend: str | None = None) -> None:
    """Prepare matplotlib for this package.

    Parameters
    ----------
    lang
        ``"zh"`` selects a CJK-capable font; ``"en"`` leaves the font stack
        alone so the default sans-serif is used.
    backend
        Matplotlib backend to select. Pass ``"Agg"`` for headless figure
        writing. ``None`` leaves the current backend untouched, which is what
        makes notebook and interactive use work.
    """
    global _configured, _font_warned

    if backend:
        # Only override when the user has not already chosen. Respecting
        # MPLBACKEND is the difference between "works in a notebook" and
        # "silently refuses to show a window".
        if not os.environ.get("MPLBACKEND"):
            matplotlib.use(backend)

    if lang == "zh":
        available = _available_fonts()
        chosen = [f for f in _CJK_FONTS if f in available]
        if chosen:
            plt.rcParams["font.sans-serif"] = chosen + ["DejaVu Sans"]
        elif not _font_warned:
            import warnings
            warnings.warn(
                "No CJK font found; Chinese labels will render as boxes. "
                f"Tried: {', '.join(_CJK_FONTS)}. Install one, e.g. "
                "`apt install fonts-noto-cjk`.",
                stacklevel=2,
            )
            _font_warned = True
        plt.rcParams["axes.unicode_minus"] = False

    _configured = True


def style_ax(ax) -> None:
    """Apply the shared grid and tick colours to an axis."""
    ax.grid(True, color=GRIDC, alpha=0.35, linewidth=0.6)
    ax.tick_params(colors=INK)


def _finish(fig, out_png: str) -> str:
    """Save and close, creating the destination directory if needed."""
    d = os.path.dirname(os.path.abspath(out_png))
    if d:
        os.makedirs(d, exist_ok=True)
    fig.savefig(out_png, dpi=150)
    plt.close(fig)
    return out_png


def _stacked_ts(sod, series, ylabels, title, out_png, lang, annot_fmt, colors):
    """Shared 3-panel time-series layout used by the position and velocity plots."""
    fig, axes = plt.subplots(3, 1, figsize=(11, 7.2), sharex=True)
    fig.suptitle(title, fontsize=12)
    for ax, d, ylab, c in zip(axes, series, ylabels, colors):
        bias, std, rms = component_stats(d)
        ax.scatter(sod, d, s=16, color=c, alpha=0.9, zorder=3)
        ax.plot(sod, d, color=c, lw=1.0, alpha=0.55, zorder=2)
        ax.axhline(0, color=ZERO, lw=0.9, ls="--", zorder=1)
        ax.axhline(bias, color=c, lw=0.8, ls=":", alpha=0.7)
        ax.set_ylabel(ylab, color=INK, fontsize=10)
        ax.text(0.01, 0.95, t(lang, annot_fmt, bias=bias, std=std, rms=rms),
                transform=ax.transAxes, va="top", fontsize=9, color=INK)
        style_ax(ax)
    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    axes[-1].set_xticks(np.arange(0, float(np.max(sod)) + 1, 300))
    fig.tight_layout(rect=[0, 0, 1, 0.97])
    return _finish(fig, out_png)


def fig_pos_error_enu_ts(sod, d_e, d_n, d_u, out_png, mode_label="DUAL_IF", lang="en"):
    """Figure 1: position error time series, one panel per ENU component."""
    return _stacked_ts(
        sod, (d_e, d_n, d_u),
        (t(lang, "axis_east_err"), t(lang, "axis_north_err"), t(lang, "axis_up_err")),
        t(lang, "title_pos_ts", mode=mode_label),
        out_png, lang, "annot_stats", (C_E, C_N, C_U),
    )


def fig_velocity_enu_ts(sod, v_e, v_n, v_u, out_png, mode_label="DUAL_IF", lang="en"):
    """Figure 3: velocity time series, one panel per ENU component."""
    return _stacked_ts(
        sod, (v_e, v_n, v_u),
        (t(lang, "axis_east_vel"), t(lang, "axis_north_vel"), t(lang, "axis_up_vel")),
        t(lang, "title_vel_ts", mode=mode_label),
        out_png, lang, "annot_stats_vel", (C_E, C_N, C_U),
    )


def fig_pos_horizontal(d_e, d_n, out_png, lang="en"):
    """Figure 2: horizontal error scatter with a 2-D RMS circle."""
    d_e = np.asarray(d_e, dtype=float)
    d_n = np.asarray(d_n, dtype=float)
    rms_h = float(np.sqrt((d_e ** 2 + d_n ** 2).mean()))

    fig, ax = plt.subplots(figsize=(6.2, 6))
    ax.axhline(0, color=ZERO, lw=0.9, ls="--")
    ax.axvline(0, color=ZERO, lw=0.9, ls="--")
    ax.scatter(d_e, d_n, s=22, color=C_E, alpha=0.85, zorder=3,
               label=t(lang, "legend_epochs", n=d_e.size))

    theta = np.linspace(0, 2 * np.pi, 200)
    ax.plot(rms_h * np.cos(theta), rms_h * np.sin(theta),
            color="#9b34a2", lw=1.3, ls="--",
            label=t(lang, "legend_rms_circle", r=rms_h))

    ax.scatter(d_e.mean(), d_n.mean(), marker="x", s=90, color="k", zorder=4,
               label=t(lang, "legend_mean_bias", e=d_e.mean(), n=d_n.mean()))

    ax.set_xlabel(t(lang, "axis_east_err"), fontsize=10)
    ax.set_ylabel(t(lang, "axis_north_err"), fontsize=10)
    ax.set_title(t(lang, "title_pos_h"), fontsize=11)
    ax.set_aspect("equal", adjustable="datalim")
    ax.legend(fontsize=9, loc="best", framealpha=0.9)
    style_ax(ax)
    fig.tight_layout()
    return _finish(fig, out_png)


def fig_clock_drift(sod, clkdot, out_png, mode_label="DUAL_IF", lang="en"):
    """Figure 4: receiver clock drift time series."""
    clkdot = np.asarray(clkdot, dtype=float)
    fig, ax = plt.subplots(figsize=(11, 3.6))
    ax.plot(sod, clkdot, color=C_U, lw=1.2, marker="o", ms=4, alpha=0.9)
    ax.axhline(clkdot.mean(), color=ZERO, lw=0.9, ls=":")
    ax.set_xlabel(t(lang, "axis_sod"), fontsize=10)
    ax.set_ylabel(t(lang, "axis_clk"), fontsize=10)
    ax.set_title(t(lang, "title_clock", mode=mode_label), fontsize=11)
    ax.text(0.01, 0.92,
            t(lang, "annot_clk", lo=float(clkdot.min()), hi=float(clkdot.max()),
              mean=float(clkdot.mean())),
            transform=ax.transAxes, va="top", fontsize=9, color=INK)
    ax.set_xticks(np.arange(0, float(np.max(sod)) + 1, 300))
    style_ax(ax)
    fig.tight_layout()
    return _finish(fig, out_png)
