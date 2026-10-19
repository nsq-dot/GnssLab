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
from matplotlib.patches import Patch

from ._strings import t
from .cycleslip import STATE_ORDER
from .stats import component_stats

__all__ = [
    "configure",
    "C_E", "C_N", "C_U", "C_SLIP", "C_SPP", "C_RTK", "INK", "GRIDC", "ZERO",
    "C_ZOOM_COARSE", "C_ZOOM_FOCUS", "ZOOM_DIRS",
    "fig_pos_error_enu_ts",
    "fig_pos_horizontal",
    "fig_velocity_enu_ts",
    "fig_clock_drift",
    "fig_slip_rate_bars",
    "fig_slip_series",
    "fig_state_composition",
    "fig_injection_outcomes",
    "fig_rtk_spp_vs_rtk_ts",
    "fig_rtk_error_enu_ts",
    "fig_rtk_accuracy_bars",
    "fig_rtk_float_vs_fixed_ts",
    "fig_rtk_ratio_hist",
    "fig_rtk_lsq_vs_kalman_ts",
    "fig_rtk_dd_cov_ratio_ts",
    "fig_rtk_mixed_signed_ts",
    "fig_rtk_spp_vs_rtk_dir_ts",
    "fig_rtk_float_vs_fixed_zoom_ts",
    "fig_rtk_lsq_vs_kalman_zoom_ts",
]

# Colour slots (blue / orange / aqua), plus ink, grid and zero-line greys.
# These are a colour-blind-safe categorical set that stays legible in both light
# and dark rendering.
C_E, C_N, C_U = "#2a78d6", "#eb6834", "#1baf7a"
INK, GRIDC, ZERO = "#1a1a1a", "#c9c9c9", "#888888"

# "A slip was reported", and the threshold line it was compared against.
# Deliberately not one of the three categorical slots: orange already means "the
# polynomial detector" in the very panel below the one this colours, and a
# detection marker is not a series.
C_SLIP = "#d62728"

# SPP and RTK are two *methods*, not two axes and not two constellations, so
# neither takes one of the three categorical slots. Grey is the coarse solution
# the eye should discount; the red is the one under discussion. Deliberately a
# separate name from C_SLIP despite the identical value: retuning the cycle-slip
# red must not move a byte in chapter 8's figures.
C_SPP, C_RTK = "#6e6e6e", "#d62728"

# The nine directional siblings of figures 8-1, 8-4 and 8-6 use blue for the
# coarse solution instead of the grey above. Same grammar as C_SPP/C_RTK -
# blue is still the coarse solution the eye discounts, red still the one under
# discussion - so the legends are unchanged and only the captions have to say
# which colour is which. The reason is legibility, not meaning: 8-4b/c/d and
# 8-6b/c/d are drawn at 3-100x the vertical zoom, where a grey line on white is
# hard to follow and the two series no longer read as "a band and a line" but as
# two curves that have to be told apart. 8-1b/c/d need no such help - they draw
# the same span as their parent - and are blue only so that the nine read as one
# set.
#
# Deliberately not C_E, which is a *blue* too: in this chapter C_E/C_N/C_U mean
# east/north/up in one figure and the three constellations in another, so
# borrowing one of them for "a method" would overload the slot.
C_ZOOM_COARSE, C_ZOOM_FOCUS = "#2a78d6", "#d62728"

# Shared y window for the SPP/RTK comparison, in metres. Fixed, and shared by
# all three panels, because the figure's one job is the *distance* between the
# two bands: a window refitted per constellation would make BDS-2's 1.13 m and
# BDS-3's 0.076 m look alike, which is the opposite of what section four
# concludes. 5e-4 m is below the smallest epoch measured (0.86 mm, BDS-3), so
# nothing is clipped; 200 m clears the worst (128 m, BDS-2's SPP). A derived
# window would also move whenever one epoch moved.
_RTK_CMP_YMIN, _RTK_CMP_YMAX = 5e-4, 200.0

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


# ---------------------------------------------------------------------------
# Cycle-slip detection (chapter 7)
# ---------------------------------------------------------------------------
# The detectors' own judgement states, coloured once here so the series figure
# and the composition figure cannot disagree about what a colour means.

_STATE_COLORS = {
    "OK": "#e6e6e6",
    "SLIP": C_SLIP,
    "INIT": ZERO,
    "GAP": C_N,
    "WARMUP": "#f2d16b",
}


def fig_slip_rate_bars(runs, out_png, lang="en", detectors=None,
                       title_key="title_slip_rate"):
    """Figure 7-5 / 7-7: reported slip rate per satellite, detectors side by side.

    `runs` is a sequence of::

        {"label": str,
         "sat":   array of satellite ids,
         "diff":  {"rate": per-satellite fraction array, "nSlip": float, "nTested": float},
         "poly":  {...same...}}

    `detectors` names the series to draw as ``(run key, legend key)`` pairs. It
    defaults to the two GF detectors of figure 7-5; the MW figure passes a single
    pair and its own title. One panel per run. The y-axis is deliberately linear
    0-100 %: the rates span two orders of magnitude (0.22 % to 5.15 %), so a log
    axis would draw them at comparable heights and misrepresent the 23x
    difference as a small one. The small values are carried by the annotation
    instead.

    The x-axis is NOT shared between panels: the 1 Hz and 30 s runs cover
    different satellite sets, so the bars would not line up.

    Bar placement is derived from the number of series rather than written out,
    which leaves the two-series case of figure 7-5 at exactly the +-width/2 it
    has always used.
    """
    detectors = tuple(detectors if detectors is not None else
                      (("diff", "legend_detector_diff"),
                       ("poly", "legend_detector_poly")))
    runs = list(runs)
    n = len(runs)
    fig, axes = plt.subplots(n, 1, figsize=(11, 7.2) if n > 1 else (11, 3.6),
                             sharey=True, squeeze=False)
    axes = axes.ravel()
    fig.suptitle(t(lang, title_key), fontsize=12)

    width = 0.38
    colours = (C_E, C_N, C_U)
    k = len(detectors)
    for ax, run in zip(axes, runs):
        sat = [str(s) for s in run["sat"]]
        x = np.arange(len(sat))

        for i, (key, legend_key) in enumerate(detectors):
            ax.bar(x + (i - (k - 1) / 2.0) * width,
                   np.asarray(run[key]["rate"]) * 100.0, width,
                   color=colours[i % len(colours)], label=t(lang, legend_key))

        ax.set_title(run["label"], fontsize=11)
        ax.set_ylabel(t(lang, "axis_slip_rate"), fontsize=10)
        ax.set_xticks(x)
        ax.set_xticklabels(sat, rotation=90, fontsize=8)

        # The bars are per satellite; the annotation is the run total, which is
        # the number the summary CSV's TOTAL row carries and the document quotes.
        lines = []
        for key, legend_key in detectors:
            d = run[key]
            n_slip, n_tested = int(d["nSlip"]), int(d["nTested"])
            total = (100.0 * n_slip / n_tested) if n_tested else 0.0
            lines.append("%s: %s" % (
                t(lang, legend_key),
                t(lang, "annot_slip_rate", nslip=n_slip, ntested=n_tested, rate=total)))
        ax.text(0.01, 0.97, "\n".join(lines), transform=ax.transAxes,
                va="top", fontsize=9, color=INK)

        ax.legend(fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    axes[-1].set_xlabel(t(lang, "axis_sat"), fontsize=10)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


def _sod_axis(ax, sod) -> None:
    """Lay the x ticks out over the data's own span, not from sod 0.

    An arc does not start at midnight (G01's begins at 24517), and a tick range
    anchored at 0 would expand the axis to cover the empty hours before it —
    squashing the data into the right-hand quarter and stacking a hundred labels
    into that quarter. The step is the first one that yields roughly ten labels.
    """
    sod = np.asarray(sod, dtype=float)
    if not sod.size:
        return
    x0, x1 = float(np.nanmin(sod)), float(np.nanmax(sod))
    if not np.isfinite(x0) or not np.isfinite(x1):
        return
    span = max(x1 - x0, 1.0)
    step = 7200.0
    for candidate in (60.0, 120.0, 300.0, 600.0, 1200.0, 1800.0, 3600.0, 7200.0):
        step = candidate
        if span / candidate <= 12:
            break
    ax.set_xticks(np.arange(np.floor(x0 / step) * step, x1 + step, step))
    ax.set_xlim(x0, x1)


def fig_slip_series(diff, poly, out_png, thr=0.030, sat="", run_label="", lang="en"):
    """Figure 7-2/7-3: one satellite's GF series, statistics and arc state.

    `diff` and `poly` are the 5-tuples from ``cycleslip.series_view`` -
    ``(sod, li, stat, flag, status)`` with ``stat`` already an absolute value -
    or None for a detector that was not run.

    Three panels, and the layout is the argument:

    * L_I alone, because it runs to tens of metres and must not share an axis
      with a millimetre statistic. Not zero-based: an arc can sit entirely above
      or below zero, and forcing the origin in would flatten it.
    * Both statistics together on a **log** axis against **one** threshold line.
      They are the same kind of quantity compared against the same number, so
      separating them into two panels would draw the threshold twice and lose the
      comparison the figure exists to make. The log axis also discards the nan
      epochs (arc start, gap, warmup) on its own, which is what is wanted.
    * Arc state, as a scatter strip rather than an image: `imshow` assumes even
      spacing across the whole span, and the first arc rarely starts at sod 0.
    """
    sod = np.asarray(diff[0], dtype=float)
    li = np.asarray(diff[1], dtype=float)
    status = np.asarray(diff[4])
    flag = np.asarray(diff[3], dtype=float)

    fig, axes = plt.subplots(3, 1, figsize=(11, 7.2), sharex=True,
                             gridspec_kw={"height_ratios": [2.2, 2.2, 1.2]})
    fig.suptitle(t(lang, "title_slip_series", sat=sat, run=run_label), fontsize=12)

    # --- panel 1: the geometry-free series itself ---
    ax = axes[0]
    ax.plot(sod, li, color=C_U, lw=1.0, alpha=0.85, zorder=2)
    marks = ((status == "SLIP", "v", C_SLIP, "legend_slip_epochs"),
             (status == "INIT", "^", ZERO, "legend_init_epochs"),
             (status == "GAP", "s", C_N, "legend_gap_epochs"))
    for mask, marker, colour, key in marks:
        if mask.any():
            ax.plot(sod[mask], li[mask], marker, color=colour, ms=6, ls="none",
                    zorder=3, label=t(lang, key))
    ax.margins(y=0.08)
    ax.set_ylabel(t(lang, "axis_li"), fontsize=10)
    n_slip = int((status == "SLIP").sum())
    n_judged = int(np.isin(status, ("OK", "SLIP")).sum())
    ax.text(0.01, 0.97,
            t(lang, "annot_slip_series", nslip=n_slip, ntested=n_judged, thr=thr),
            transform=ax.transAxes, va="top", fontsize=9, color=INK)
    ax.legend(fontsize=9, loc="lower left", framealpha=0.9)
    style_ax(ax)

    # --- panel 2: both statistics against the one threshold ---
    ax = axes[1]
    for series, colour, key in ((diff, C_E, "legend_detector_diff"),
                                (poly, C_N, "legend_detector_poly")):
        if series is None:
            continue
        ax.plot(series[0], series[2], color=colour, lw=1.0, alpha=0.9, label=t(lang, key))
    ax.axhline(thr, color=C_SLIP, ls="--", lw=1.0,
               label=t(lang, "legend_threshold", thr=thr))
    ax.set_yscale("log")
    ax.set_ylabel(t(lang, "axis_stat"), fontsize=10)
    ax.legend(fontsize=9, loc="lower left", framealpha=0.9, ncol=3)
    style_ax(ax)

    # --- panel 3: arc / gap state, one row per detector ---
    ax = axes[2]
    rows = [(1.0, diff, "legend_detector_diff"), (0.0, poly, "legend_detector_poly")]
    for y, series, key in rows:
        if series is None or series[0].size == 0:
            continue
        st = np.asarray(series[4])
        for state in _STATE_COLORS:
            mask = st == state
            if mask.any():
                ax.scatter(series[0][mask], np.full(int(mask.sum()), y),
                           marker="s", s=10, linewidths=0,
                           color=_STATE_COLORS[state], zorder=3)
    ax.set_yticks([1.0, 0.0])
    ax.set_yticklabels([t(lang, "legend_detector_diff"),
                        t(lang, "legend_detector_poly")], fontsize=9)
    ax.set_ylim(-0.6, 1.6)
    ax.set_ylabel(t(lang, "axis_state"), fontsize=10)

    handlers = [Patch(facecolor=_STATE_COLORS[s], edgecolor=INK, linewidth=0.4,
                      label=t(lang, "lbl_state_" + s.lower()))
                for s in _STATE_COLORS]
    # Below the axes, not inside them: the strip is short and fully occupied by
    # the OK band, so an inset legend would sit on top of the data.
    ax.legend(handles=handlers, fontsize=9, loc="upper center",
              bbox_to_anchor=(0.5, -0.45), ncol=5, framealpha=0.9)
    style_ax(ax)

    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    _sod_axis(axes[-1], sod)
    fig.tight_layout(rect=[0, 0.04, 1, 0.97])
    return _finish(fig, out_png)


def fig_state_composition(bars, out_png, lang="en"):
    """Figure 7-6: how the detectors spent their epochs, as stacked bars.

    `bars` is a sequence of ``(label, counts)`` where `counts` is a 5-element
    array in ``cycleslip.STATE_ORDER``.

    Shares are taken over the epoch ROWS, not over the judged epochs, which is
    the chapter-7 document's own convention - the point of the figure is that
    WARMUP grows with the sampling interval, and WARMUP epochs are by definition
    not judged.
    """
    bars = list(bars)
    fig, ax = plt.subplots(figsize=(11, 3.6))
    fig.suptitle(t(lang, "title_state_composition"), fontsize=12)

    y = np.arange(len(bars))
    left = np.zeros(len(bars))
    for i, state in enumerate(STATE_ORDER):
        vals = np.array([float(np.asarray(b[1], dtype=float)[i]) for b in bars])
        totals = np.array([float(np.asarray(b[1], dtype=float).sum()) for b in bars])
        pct = np.divide(vals * 100.0, totals, out=np.zeros_like(vals), where=totals > 0)
        ax.barh(y, pct, left=left, color=_STATE_COLORS[state],
                edgecolor="white", linewidth=0.6,
                label=t(lang, "lbl_state_" + state.lower()))
        left += pct

    ax.set_yticks(y)
    ax.set_yticklabels([b[0] for b in bars], fontsize=9)
    ax.set_xlim(0, 100)
    ax.set_xlabel(t(lang, "axis_share"), fontsize=10)
    ax.legend(fontsize=9, ncol=5, loc="lower center", bbox_to_anchor=(0.5, -0.46),
              framealpha=0.9)
    style_ax(ax)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    return _finish(fig, out_png)


#: The detector rows of figure 7-4, top to bottom, with the string key each
#: one's label comes from. A panel draws only those it actually has data for,
#: so a run without MW still renders as it did before MW was scored.
_INJECTION_ROWS = (("diff", "legend_detector_diff"),
                   ("poly", "legend_detector_poly"),
                   ("mw", "legend_detector_mw"))


def fig_injection_outcomes(panels, out_png, lang="en"):
    """Figure 7-4: per injected case, what each detector decided.

    `panels` is a sequence of::

        {"title":  str,
         "labels": [case label, ...],
         "expect": [expected state, ...],
         "status": {"diff": [reported state, ...], "poly": [...]},
         "score":  {"diff": {"tp": int, "n": int, "rate": float}, "poly": {...}}}

    One panel per run - the 1 Hz and 30 s manifests hold different case sets, so
    they cannot share an axis.

    Three rows when the MW run is present, two when it is not. Adding MW is what
    makes the figure carry the whole story: the GF detectors miss the
    geometry-free null space and MW misses every ``dN1 == dN2`` case, so a column
    of answers that stopped at two detectors would show only half the misses.

    A square per (case, detector). Hit = aqua, miss = red, adversarial case
    correctly declined = aqua, adversarial case where a slip was reported = the
    other orange. The cell carries the reported state verbatim, because that
    token is what the C++ wrote and what the document's tables quote.
    """
    panels = list(panels)
    n = len(panels)

    def rows_of(panel):
        return [m for m, _ in _INJECTION_ROWS if m in panel["status"]]

    legend_key = dict(_INJECTION_ROWS)
    # Sized from the tallest panel, so a run that lacks MW does not leave its
    # axes cramped next to one that has it.
    n_rows = max(len(rows_of(p)) for p in panels)
    fig, axes = plt.subplots(n, 1, figsize=(11, (2.5 + 0.62 * n_rows) * n),
                             squeeze=False)
    axes = axes.ravel()
    fig.suptitle(t(lang, "title_injection_outcomes"), fontsize=12)

    for ax, panel in zip(axes, panels):
        rows = rows_of(panel)
        labels = list(panel["labels"])
        expect = list(panel["expect"])
        x = np.arange(len(labels))

        for r, mode in enumerate(rows):
            y = float(len(rows) - 1 - r)          # diff on top
            statuses = list(panel["status"][mode])
            for xi, (want, got) in enumerate(zip(expect, statuses)):
                if want == "SLIP":
                    ok = got == "SLIP"
                    colour = C_U if ok else C_SLIP
                else:
                    ok = got == want
                    colour = C_U if ok else C_N
                ax.scatter(xi, y, marker="s", s=620, color=colour,
                           edgecolors=INK, linewidths=0.5, zorder=3)
                ax.text(xi, y, got, ha="center", va="center", fontsize=8,
                        color=INK, zorder=4)

        ax.set_xticks(x)
        ax.set_xticklabels(labels, rotation=30, ha="right", fontsize=9)
        ax.set_yticks([float(len(rows) - 1 - r) for r in range(len(rows))])
        ax.set_yticklabels([t(lang, legend_key[m]) for m in rows], fontsize=9)
        # Headroom above the top row, in row units rather than as a fixed
        # fraction of the axis. Both the score annotation (upper left, one line
        # per detector) and the outcome legend (upper right) live up there, and
        # a fixed fraction stops being enough once there are three of each.
        ax.set_ylim(-0.6, len(rows) - 1 + 1.4)
        ax.set_title(panel["title"], fontsize=11)

        lines = []
        for mode in rows:
            s = panel["score"][mode]
            label = t(lang, legend_key[mode])
            lines.append(t(lang, "annot_injection", run=label, tp=int(s["tp"]),
                           n=int(s["n"]), rate=100.0 * s["rate"]))
        ax.text(0.01, 0.95, "\n".join(lines), transform=ax.transAxes,
                va="top", fontsize=9, color=INK)

        handles = [Patch(facecolor=C_U, edgecolor=INK, linewidth=0.4,
                         label=t(lang, "lbl_outcome_hit")),
                   Patch(facecolor=C_SLIP, edgecolor=INK, linewidth=0.4,
                         label=t(lang, "lbl_outcome_miss")),
                   Patch(facecolor=C_N, edgecolor=INK, linewidth=0.4,
                         label=t(lang, "lbl_outcome_wrong"))]
        # Upper right: the lower corners hold cells, and the annotation already
        # claims the upper left.
        ax.legend(handles=handles, fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


# ---------------------------------------------------------------------------
# The MW combination: its own results, and the cross-check against GF
# ---------------------------------------------------------------------------

def fig_mw_series(view, out_png, sat="", run_label="", lang="en", deviation=True):
    """Figures 7-1 and 7-8: one satellite's MW combination and the epochs it
    reported.

    `view` is the 5-tuple from ``cycleslip.series_view(path, "mw")`` —
    ``(sod, mw, |deviation|, flag, status)``, with the status already translated
    out of MW's 0/1 flag. Both the combination and the deviation arrive in
    **cycles**, not metres: ``series_view`` has already divided by the wide-lane
    wavelength, because the MW combination *is* the wide-lane ambiguity N_W and
    the chapter states it as an integer count. The GF figures keep metres - see
    `cycleslip.mw_wavelength` for why the two differ.

    `deviation` adds a second panel underneath holding the bias that the
    detector compares its thresholds against, in cycles. Its first sample is
    absent: the first epoch has no mean carried in.

    MW publishes no residual column, so this bias is reconstructed by
    `series_view` rather than read. The detector has two thresholds and neither
    is drawn: one is fixed - ``minCycles(2.0) * wavelengthMW``, which is exactly
    2.0 in these units - and the other is adaptive, ``4*sqrt(varMW)``, which
    needs a column the C++ does not write. See docs/roadmap.md; the GF figures
    draw their fixed threshold, MW does not.

    Figure 7-8 keeps the panel; figure 7-1 drops it, because §一 is asking
    whether the detector runs at all rather than how it decides, and the
    combination itself answers that.
    """
    sod = np.asarray(view[0], dtype=float)
    mw = np.asarray(view[1], dtype=float)
    dev = np.asarray(view[2], dtype=float)
    status = np.asarray(view[4])
    slip = status == "SLIP"

    if deviation:
        fig, axes = plt.subplots(2, 1, figsize=(11, 6.4), sharex=True,
                                 gridspec_kw={"height_ratios": [2.0, 1.5]})
    else:
        fig, single = plt.subplots(figsize=(11, 4.2))
        axes = [single]
    fig.suptitle(t(lang, "title_mw_series", sat=sat, run=run_label), fontsize=12)

    ax = axes[0]
    ax.plot(sod, mw, color=C_U, lw=1.0, alpha=0.85, zorder=2,
            label=t(lang, "legend_detector_mw"))
    if slip.any():
        ax.plot(sod[slip], mw[slip], "v", color=C_SLIP, ms=6, ls="none", zorder=3,
                label=t(lang, "legend_slip_epochs"))
    ax.margins(y=0.08)
    ax.set_ylabel(t(lang, "axis_mw"), fontsize=10)
    n_slip = int(slip.sum())
    ax.text(0.01, 0.97,
            t(lang, "annot_slip_rate", nslip=n_slip, ntested=int(sod.size),
              rate=(100.0 * n_slip / sod.size) if sod.size else 0.0),
            transform=ax.transAxes, va="top", fontsize=9, color=INK)
    ax.legend(fontsize=9, loc="lower left", framealpha=0.9)
    style_ax(ax)

    if deviation:
        ax = axes[1]
        ax.plot(sod, dev, color=C_U, lw=0.9, alpha=0.85, zorder=2)
        if slip.any():
            ax.plot(sod[slip], dev[slip], "v", color=C_SLIP, ms=6, ls="none",
                    zorder=3)
        ax.set_yscale("log")
        # `axis_stat_mw`, not `axis_stat`: this panel is MW, which
        # `series_view` has already converted to cycles.
        ax.set_ylabel(t(lang, "axis_stat_mw"), fontsize=10)
        style_ax(ax)

    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    _sod_axis(axes[-1], sod)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    return _finish(fig, out_png)


def fig_crosscheck(panels, out_png, lang="en"):
    """Figure 7-9: where both combinations report a slip, and where only one does.

    `panels` is a sequence of::

        {"label":    str,
         "sats":     satellite ids, in the y order to draw,
         "both":     iterable of (sat, sod) that both detectors reported,
         "gf_only":  ... reported by the GF difference detector alone,
         "mw_only":  ... reported by MW alone,
         "counts":   (n_gf, n_mw, n_both)}

    One row per satellite, one marker per reported epoch, which is the layout
    that shows the point: on 1 Hz the two combinations report nearly the same
    epochs, and on 30 s the GF detector's extra flags have no MW counterpart at
    all. The counts are annotated because the figure is about that ratio rather
    than about any individual epoch.

    The x-axis is NOT shared: the 1 Hz file starts in the afternoon and the 30 s
    sample at midnight, so one axis covering both would squash the 30 s panel
    into the left edge.
    """
    panels = list(panels)
    fig, axes = plt.subplots(len(panels), 1,
                             figsize=(11, 8.0) if len(panels) > 1 else (11, 4.4),
                             squeeze=False)
    axes = axes.ravel()
    fig.suptitle(t(lang, "title_crosscheck"), fontsize=12)

    # Drawn largest group first so the rarer "both" markers are not buried: on
    # 30 s there are two of them against fifty-four GF-only flags.
    marks = (("gf_only", "o", C_E, "legend_xcheck_gf_only"),
             ("mw_only", "s", C_U, "legend_xcheck_mw_only"),
             ("both", "D", C_SLIP, "legend_xcheck_both"))

    for ax, panel in zip(axes, panels):
        sats = [str(s) for s in panel["sats"]]
        index = {s: i for i, s in enumerate(sats)}
        sod_here = []
        for key, marker, colour, legend_key in marks:
            pts = [(index[str(s)], float(v)) for s, v in panel[key] if str(s) in index]
            if not pts:
                continue
            xs = [p[1] for p in pts]
            ys = [p[0] for p in pts]
            sod_here.extend(xs)
            ax.plot(xs, ys, marker, color=colour, ms=5, ls="none", alpha=0.85,
                    label=t(lang, legend_key))

        ax.set_yticks(range(len(sats)))
        ax.set_yticklabels(sats, fontsize=8)
        ax.set_ylim(-0.8, max(len(sats) - 0.2, 0.8))
        ax.set_title(panel["label"], fontsize=11)
        n_gf, n_mw, n_both = panel["counts"]
        ax.text(0.01, 0.97,
                t(lang, "annot_crosscheck", gf=n_gf, mw=n_mw, both=n_both),
                transform=ax.transAxes, va="top", fontsize=9, color=INK)
        ax.legend(fontsize=9, loc="upper right", framealpha=0.9, ncol=3)
        ax.set_xlabel(t(lang, "axis_sod"), fontsize=10)
        _sod_axis(ax, np.asarray(sod_here, dtype=float))
        style_ax(ax)

    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


def fig_nullspace(gf_view, mw_view, out_png, sat="", dn1=0, dn2=0, epoch=None,
                  thr=0.030, span=120.0, lang="en",
                  title_key="title_nullspace"):
    """Figures 7-10 and 7-11: one injected slip only one of the two sees.

    The layout is the same in both directions, so `title_key` is what says which
    combination is the blind one here: ``title_nullspace`` for the geometry-free
    null space, where GF is flat and MW steps, and ``title_nullspace_mw`` for
    the mirror case ``dN1 == dN2``, where MW is flat and GF steps.

    Two panels over the same epochs: the GF difference detector's statistic
    against its threshold, and the MW combination. The jump of ``(dn1, dn2)``
    cycles sits at `epoch`, and the figure exists to show one panel flat and the
    other one stepping.

    The two panels are deliberately in **different units**. The upper one is the
    GF statistic in metres, threshold included, because the GF combination is
    not an integer multiple of any wavelength. The lower one is the MW
    combination in cycles - ``series_view`` divides by the wide-lane wavelength -
    so the step that this figure is about can be read off directly as the
    ``dn1``/``dn2`` pair named in the title.

    GF is blind to this slip by construction rather than by tuning:
    ``dn1 * lambda1 - dn2 * lambda2`` is zero for (77, 60) on GPS, so the
    combination the detector thresholds does not change at all. That is what
    "null space" means here, and no threshold can recover it.

    The window is clipped to `span` seconds either side of the injection. Both
    series run for thousands of epochs, and a full-arc view would compress a
    five-cycle step into the line width - the figure would be honest and
    unreadable at the same time.
    """
    gsod = np.asarray(gf_view[0], dtype=float)
    gstat = np.asarray(gf_view[2], dtype=float)
    msod = np.asarray(mw_view[0], dtype=float)
    mmw = np.asarray(mw_view[1], dtype=float)

    if epoch is not None:
        keep_g = (gsod >= epoch - span) & (gsod <= epoch + span)
        keep_m = (msod >= epoch - span) & (msod <= epoch + span)
        gsod, gstat = gsod[keep_g], gstat[keep_g]
        msod, mmw = msod[keep_m], mmw[keep_m]

    fig, axes = plt.subplots(2, 1, figsize=(11, 6.4), sharex=True,
                             gridspec_kw={"height_ratios": [1.5, 2.0]})
    fig.suptitle(t(lang, title_key, sat=sat, dn1=dn1, dn2=dn2), fontsize=12)

    ax = axes[0]
    ax.plot(gsod, gstat, color=C_E, lw=1.1, alpha=0.9,
            label=t(lang, "legend_detector_diff"))
    ax.axhline(thr, color=C_SLIP, ls="--", lw=1.0,
               label=t(lang, "legend_threshold", thr=thr))
    ax.set_yscale("log")
    ax.set_ylabel(t(lang, "axis_stat"), fontsize=10)
    ax.legend(fontsize=9, loc="lower left", framealpha=0.9, ncol=2)
    style_ax(ax)

    ax = axes[1]
    ax.plot(msod, mmw, color=C_U, lw=1.1, alpha=0.9,
            label=t(lang, "legend_detector_mw"))
    ax.margins(y=0.12)
    ax.set_ylabel(t(lang, "axis_mw"), fontsize=10)
    ax.legend(fontsize=9, loc="lower left", framealpha=0.9)
    style_ax(ax)

    if epoch is not None:
        for ax in axes:
            ax.axvline(epoch, color=INK, ls=":", lw=1.2)
        axes[0].text(0.99, 0.03, t(lang, "annot_nullspace", dn1=dn1, dn2=dn2),
                     transform=axes[0].transAxes, ha="right", va="bottom",
                     fontsize=9, color=INK)

    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    _sod_axis(axes[-1], msod if msod.size else gsod)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    return _finish(fig, out_png)


# ---------------------------------------------------------------------------
# RTK float accuracy (chapter 8)
# ---------------------------------------------------------------------------
# `_stacked_ts` above is the same three-panel layout, but it takes one series
# per panel and draws markers over every sample. Neither fits here: each panel
# carries three constellations, and a run is 7934 epochs, so a marker per epoch
# would be a smear rather than a scatter. This is a sibling rather than a
# parameterisation of it, which is why `_stacked_ts` is left exactly as it was -
# the chapter-1 figures it draws must not move.

def _break_gaps(sod, y):
    """Copy `y` with a NaN where the epochs are not contiguous.

    The run has a single ~130-epoch hole in it (the receiver drops out around
    sod 26200). A line drawn straight through it crosses the whole panel
    diagonally and reads as a two-minute excursion of the solution, which is
    the opposite of what happened. The threshold is five times the median
    sampling interval, so the one- and two-epoch holes elsewhere - which are
    real, and short enough to be spanned honestly - are left alone.
    """
    y = np.array(y, dtype=float, copy=True)
    sod = np.asarray(sod, dtype=float)
    if sod.size > 2:
        step = float(np.median(np.diff(sod)))
        if step > 0:
            holes = np.nonzero(np.diff(sod) > 5.0 * step)[0]
            y[holes] = np.nan
    return y


#: The absolute sod figure 8-1 stops drawing at - and the cap its three
#: directional siblings stop at too. The run itself carries on to 32578; the
#: tail is left out on request, not because the data ends there. Absolute clock
#: time rather than an offset from the run's first epoch, the way the six
#: figures that still take a *window* do (_ZOOM_WINDOW_8_4 and _ZOOM_WINDOW_8_6),
#: because these four figures' own x axis is already in sod.
#:
#: **Only the drawn series is clipped.** The bias/sigma/RMS printed inside the
#: panels keep coming from the `stats` the caller passes, so they still describe
#: the whole 7934 epochs and still agree with the chapter's tables - that
#: agreement is the reason those numbers are not recomputed here. The x label
#: says where the axis stops, so the shorter curve cannot be read as a run that
#: was shorter.
_WINDOW_8_1_SOD_MAX = 31200.0


def _in_8_1_span(sod, sod_max=_WINDOW_8_1_SOD_MAX):
    """Which of one series' epochs figure 8-1 draws, as a mask over its sod.

    One function rather than ``sod <= _WINDOW_8_1_SOD_MAX`` written out in four
    places - figure 8-1 and figures 8-1b/c/d - because the siblings *are* 8-1
    read one direction at a time. A reader comparing a sibling against its
    parent is comparing two views of the same epochs, and a sibling that quietly
    covered a different stretch would make that comparison a lie. A cap written
    in four places is a cap that drifts apart.

    `sod_max` is a parameter rather than the constant read directly so the
    helper's contract is complete on its own, and so a test can pin the boundary
    without reaching into the module's state.
    """
    return np.asarray(sod, dtype=float) <= float(sod_max)


def fig_rtk_spp_vs_rtk_ts(modes, out_png, rover="", lang="en"):
    """Figure 8-1: SPP and RTK 3-D deviation from the reference, per constellation.

    `modes` is a sequence of ``(label, sod, stats)``, where `stats` is what
    ``rtk.error_stats`` returns - the same dict :func:`fig_rtk_accuracy_bars`
    takes, so the numbers printed inside these panels and the numbers in the
    console table and in docs/rtk.md come from one computation rather than
    three. `label` names the constellation and titles that panel.

    **Every point is a distance from the reference coordinate, and that is all
    it is.** Both columns are drawn as they come - no subtraction, no scaling -
    which is what makes this the figure for the chapter's headline ratio
    (121x / 30x / 330x, the console table's rightmost column). Three things
    have to be said beside it for that reading to survive:

    * The reference is whatever the caller passes as ``--ref-xyz``; without it
      the base station's RINEX header coordinate, a nominal position that on
      this equipment sits 24.9 m from the antenna. "Deviation from" is
      therefore the honest axis title; "accuracy" is not.
    * The RTK column is tied to that reference **by construction**:
      ``apps/rtk.cpp`` writes ``rtk: = xyzRover + dxyzRTK``, and the double
      difference resolves the rover relative to the base station, whose
      position is whatever coordinate the solver was given. Its 0.01-0.03 m
      offset is the double difference's own residual (the zero baseline is a
      physical zero), not evidence that the method is accurate to 3 cm.
    * The SPP column is an absolute solution, and its offset is its own error
      rather than a datum term (0.01 / 3.0 / 1.3 m on this data). The offset is
      annotated on every panel for exactly this reason: ``annot_rtk_vs_rms``
      prints it next to the RMS it is part of, so the gap between the two bands
      can be read as the offset it largely is.

    What this figure deliberately does **not** show is the other half of the
    comparison - the same two columns with their offsets removed, which is what
    answers "how repeatable is each solution", and where part of what is left
    turns out to be a slow drift rather than noise. Those numbers live in
    section four's table and prose (16x / 5.2x / 29x centred, 3.7x / 3.5x / 3.2x
    epoch-to-epoch), next to the 16x / 5.9x / 32x this panel annotates.

    **The axis is linear, and that is the point.** SPP's 3-D RMS is 2.5-6.6 m
    and RTK's is 0.08-1.13 m: the RTK band lies in the bottom few percent of the
    window while the SPP band runs across the top of it, and the reader can see
    both *how large* the SPP error is and that the differential solution is two
    orders below it. A logarithmic axis shows the ratio instead and hides the
    size - which is why this chapter's figures are all linear (chapter 8's
    figures, like the rest of the report). Each panel is scaled to its own data -
    the constellations differ by an order of magnitude, so a shared linear window
    would press the smaller two onto the bottom edge; comparing them is the
    annotation's job, which is where the numbers were read from anyway.

    A magnitude of exactly zero still becomes a NaN here rather than a plotted
    point. On this axis the resulting gap sits on the bottom edge and is a
    line-width wide, so the curve reads as continuous; keeping the rule means
    this panel and figures 8-4 and 8-6 treat a zero the same way.

    The drawn series is clipped to ``_WINDOW_8_1_SOD_MAX``; the panel statistics
    are not - they are the caller's, over the whole run. See that constant. The
    three directional siblings (:func:`fig_rtk_spp_vs_rtk_dir_ts`) draw this
    same set of epochs, through the same :func:`_in_8_1_span` - so "figure 8-1
    read one direction at a time" is a fact about the two, not a caption's
    promise.
    """
    modes = [(str(m[0]), np.asarray(m[1], dtype=float), m[2]) for m in modes]
    # `n` is taken before the clip on purpose: it is the run's epoch count, and
    # the panel numbers below - which are not clipped either - describe the same
    # run.
    n = max((int(sod.size) for _l, sod, _s in modes), default=0)

    # Clip what is drawn. `keep` indexes the stats' ENU arrays, which
    # error_stats built from these same epochs, so it lines up with `sod` one
    # for one - both get cut with the same index.
    clipped = []
    for label, sod, stats in modes:
        keep = np.flatnonzero(_in_8_1_span(sod))
        clipped.append((label, sod[keep], stats, keep))
    modes = clipped

    fig, axes = plt.subplots(3, 1, figsize=(11, 9.6), sharex=True)
    fig.suptitle(t(lang, "title_rtk_spp", rover=rover or "-", n=n), fontsize=12)

    for k, (ax, (label, sod, stats, keep)) in enumerate(zip(axes, modes)):
        notes = []
        for key, color, lkey in (("spp", C_SPP, "legend_spp"),
                                 ("rtk", C_RTK, "legend_rtk")):
            e, nu, u = (np.asarray(a, dtype=float)[keep]
                        for a in stats[key + "_enu"])
            mag = np.sqrt(e ** 2 + nu ** 2 + u ** 2)
            mag = np.where(mag > 0.0, mag, np.nan)
            ax.plot(sod, _break_gaps(sod, mag), color=color, lw=0.7, alpha=0.9,
                    zorder=2, label=t(lang, lkey))
            # The offset is printed beside the RMS it is part of, because it is
            # what separates the two bands: `rms_3d` is about the reference,
            # the offset is the distance between the series' own mean and that
            # reference. Taken as a 3-vector magnitude (norm of the ENU bias),
            # so the three panels stay comparable - a per-axis figure could not
            # be.
            notes.append(t(lang, "annot_rtk_vs_rms", method=t(lang, lkey),
                           rms=stats[key]["rms_3d"],
                           off=float(np.linalg.norm(stats[key]["bias"]))))
        ratio = stats["ratio"]
        notes.append(t(lang, "annot_rtk_vs_gain",
                       ratio=("%.0f×" % ratio) if np.isfinite(ratio) else "-"))

        ax.set_title(label, loc="left", fontsize=10, color=INK)
        # The annotation needs a backing here, which the sibling figure gets
        # away without: the window runs the series straight through it
        # straight through this corner of every panel.
        ax.text(0.01, 0.97, "\n".join(notes), transform=ax.transAxes, va="top",
                fontsize=8, color=INK,
                bbox=dict(facecolor="white", alpha=0.82, edgecolor="none",
                          pad=2.0))
        if k == 0:
            ax.legend(fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    # One label for the middle panel only: the three panels carry the same
    # quantity, so repeating it three times would say nothing new.
    axes[1].set_ylabel(t(lang, "axis_rtk_mag"), fontsize=10)
    # Each panel is scaled to its own data, with its bottom pinned to zero. The
    # constellations' errors differ by an order of magnitude (BDS-2's SPP runs
    # to ~55 m, GPS's to ~5), and on a shared *linear* axis the smaller two
    # would be pressed onto the bottom edge. What the shared height used to
    # carry - which constellation is worse - is on the panel as numbers.
    for ax in axes:
        ax.set_ylim(bottom=0.0)
    axes[-1].set_xlabel(t(lang, "axis_sod_to", to="%d" % _WINDOW_8_1_SOD_MAX),
                        fontsize=10)
    _sod_axis(axes[-1], np.concatenate([s for _l, s, _s, _k in modes])
              if modes else np.asarray([], dtype=float))
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


def fig_rtk_error_enu_ts(modes, out_png, rover="", lang="en"):
    """Figure 8-2: RTK float ENU error over time, three constellations overlaid.

    `modes` is a sequence of ``(label, sod, e, n, u)``, drawn in that order and
    coloured from the shared categorical slots, so the colour a constellation
    gets here is the colour it gets in the bar chart. Each series carries its
    own time base: the three modes come from three separate runs, and while the
    chapter's runs do share an epoch grid, nothing in the file format promises
    it - a constellation that loses one satellite loses whole epochs with it,
    and plotting a series against another run's `sod` would put the samples at
    the wrong times without looking wrong.

    **Every epoch is plotted and nothing is downsampled.** 7934 samples over an
    eleven-inch axis at 150 dpi is roughly five epochs per pixel column, which
    is why the series is drawn as a thin line with no markers: markers would
    merge into a band and would claim a per-epoch resolution the axis cannot
    show anyway. The epoch count is in the title, so the density is stated
    rather than implied.

    The x ticks come from `_sod_axis` rather than from zero: a zero-based tick
    range would stretch the axis back over the six hours before the pass starts
    and squash the data into its right-hand quarter.
    """
    # Two copies on purpose: the statistics are taken over every epoch
    # including the ones a NaN break is inserted at, and only the drawn line
    # carries the breaks. `component_stats` propagates NaN, so annotating from
    # the plotted array would print three NaNs per panel.
    raw = [(str(m[0]), np.asarray(m[1], dtype=float),
            np.asarray(m[2], dtype=float), np.asarray(m[3], dtype=float),
            np.asarray(m[4], dtype=float)) for m in modes]
    series = [(label, sod, _break_gaps(sod, e), _break_gaps(sod, nu),
               _break_gaps(sod, u)) for label, sod, e, nu, u in raw]
    colors = (C_E, C_N, C_U)
    ylabels = (t(lang, "axis_east_err"), t(lang, "axis_north_err"),
               t(lang, "axis_up_err"))

    n = max((int(sod.size) for _l, sod, *_rest in series), default=0)
    fig, axes = plt.subplots(3, 1, figsize=(11, 7.2), sharex=True)
    fig.suptitle(t(lang, "title_rtk_ts", rover=rover or "-", n=n), fontsize=12)

    for k, (ax, ylab) in enumerate(zip(axes, ylabels)):
        notes = []
        for (label, sod, e, nu, u), (rlabel, _rsod, re_, rn, ru), c in zip(
                series, raw, colors):
            d = (e, nu, u)[k]
            bias, std, rms = component_stats((re_, rn, ru)[k])
            ax.plot(sod, d, color=c, lw=0.7, alpha=0.9, zorder=2, label=rlabel)
            notes.append(t(lang, "annot_stats_rtk", mode=rlabel, bias=bias,
                           std=std, rms=rms))
        ax.axhline(0, color=ZERO, lw=0.9, ls="--", zorder=1)
        ax.set_ylabel(ylab, color=INK, fontsize=10)
        ax.text(0.01, 0.97, "\n".join(notes), transform=ax.transAxes, va="top",
                fontsize=8, color=INK)
        if k == 0:
            # The legend sits in the east panel, which is the one the
            # constellations actually differ in; repeating it three times would
            # cost a fifth of each panel to say the same thing.
            ax.legend(fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    # The tick span is the union of the series' own time bases, not the first
    # one's: a mode that starts late or ends early must not push the ticks off
    # the data it does have.
    _sod_axis(axes[-1], np.concatenate([s for _l, s, *_r in series])
              if series else np.asarray([], dtype=float))
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    return _finish(fig, out_png)


#: The seven quantities the accuracy bar chart draws, per constellation, in x
#: order: ``(label key, accessor, signed?)``. The accessor takes the dict
#: ``rtk.error_stats`` returns. Bias is signed and the rest are magnitudes,
#: which is the distinction the chart's log axis makes explicit.
_RTK_BAR_ITEMS = (
    ("lbl_bar_bias_e", lambda s: s["rtk"]["bias"][0], True),
    ("lbl_bar_bias_n", lambda s: s["rtk"]["bias"][1], True),
    ("lbl_bar_bias_u", lambda s: s["rtk"]["bias"][2], True),
    ("lbl_bar_sigma_e", lambda s: s["rtk"]["std"][0], False),
    ("lbl_bar_sigma_n", lambda s: s["rtk"]["std"][1], False),
    ("lbl_bar_sigma_u", lambda s: s["rtk"]["std"][2], False),
    ("lbl_bar_rms3d", lambda s: s["rtk"]["rms_3d"], False),
)


def fig_rtk_accuracy_bars(series, out_png, rover="", lang="en"):
    """Figure 8-3: the comparison table as a chart.

    `series` is a sequence of ``(label, stats)`` where `stats` is what
    ``rtk.error_stats`` returns. Seven quantities per constellation - the three
    per-axis biases, the three per-axis sigmas, and the 3-D RMS - grouped by
    constellation.

    The y axis is **linear and unsigned**. The three biases are millimetres to
    centimetres against a 3-D RMS of 0.08-1.13 m, so their bars are short - and
    that is the figure's point: the bias and the sigma are different kinds of
    quantity, read side by side, and the meters-scale column is the one that
    dominates. Every bar carries its own value as a label, signed for the bias,
    so nothing depends on a bar's height to be read; the axis label says the
    sign lives in the label.

    Values are printed on the bars, because a reader checking this figure
    against the console table should not have to estimate from a pixel height.
    """
    series = list(series)
    n_items = len(_RTK_BAR_ITEMS)
    n_modes = max(len(series), 1)
    x = np.arange(n_items, dtype=float)
    # Same +-width/2 layout the chapter-7 bar figures use, derived from the
    # series count rather than written out, so one constellation would centre
    # on the tick instead of sitting half a slot to its left.
    width = 0.8 / n_modes
    colors = (C_E, C_N, C_U)
    classes = (t(lang, "legend_bar_bias"), t(lang, "legend_bar_sigma"),
               t(lang, "legend_bar_rms"))

    fig, ax = plt.subplots(figsize=(11, 4.6))
    fig.suptitle(t(lang, "title_rtk_bars", rover=rover or "-"), fontsize=12)

    values = []
    for i, (label, stats) in enumerate(series):
        heights = []
        for key, get, signed in _RTK_BAR_ITEMS:
            v = float(np.asarray(get(stats), dtype=float).ravel()[0])
            heights.append(v)
        values.append(heights)
        # abs() for the bar height, the signed value for the label. A zero
        # cannot occur in these quantities (a sigma of exactly zero is a
        # degenerate run), so the floor is a guard rather than a correction -
        # it only keeps the label from being placed at y = 0.
        heights = np.abs(np.asarray(heights, dtype=float))
        heights = np.where(heights > 0.0, heights, 1e-9)
        cls = [0 if s else (2 if k == n_items - 1 else 1)
               for k, (_key, _get, s) in enumerate(_RTK_BAR_ITEMS)]
        for j, c in enumerate(cls):
            ax.bar(x[j] + (i - (n_modes - 1) / 2.0) * width, heights[j], width,
                   color=colors[c % len(colors)])

    for i, heights in enumerate(values):
        for j, v in enumerate(heights):
            key, _get, signed = _RTK_BAR_ITEMS[j]
            ax.text(x[j] + (i - (n_modes - 1) / 2.0) * width,
                    max(abs(v), 1e-9) * 1.08,
                    t(lang, "annot_bar_signed" if signed else "annot_bar_plain",
                      v=v),
                    ha="center", va="bottom", rotation=90, fontsize=6.5,
                    color=INK)

    # Linear, like the rest of this chapter's figures: the bars show the actual
    # metres. Values a bar cannot show - a bias of a few millimetres next to a
    # decimetre RMS - are printed on the bar itself by annot_bar_*, so nothing
    # becomes unreadable by dropping the log scale.
    finite = [abs(v) for heights in values for v in heights if abs(v) > 0.0]
    if finite:
        ax.set_ylim(0.0, max(finite) * 1.35)
    ax.set_xticks(x)
    ax.set_xticklabels([t(lang, k) for k, _g, _s in _RTK_BAR_ITEMS], fontsize=9)
    ax.set_ylabel(t(lang, "axis_rtk_abs"), fontsize=10)
    # Colour encodes the class, so the class legend is the one to draw; the
    # constellation is already the x tick and needs no second legend. The first
    # bar's own label is reused for its colour, which keeps the three patches
    # in the order they appear along x.
    handles = [Patch(facecolor=colors[i], edgecolor=INK, linewidth=0.4,
                     label=classes[i]) for i in range(3)]
    ax.legend(handles=handles, fontsize=9, loc="upper left", framealpha=0.9)
    style_ax(ax)
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    return _finish(fig, out_png)


def _magnitude(enu):
    """3-D magnitude of an ``(E, N, U)`` triple, as a metre array."""
    e, nu, u = (np.asarray(a, dtype=float) for a in enu)
    return np.sqrt(e ** 2 + nu ** 2 + u ** 2)


def fig_rtk_float_vs_fixed_ts(modes, out_png, rover="", lang="en", threshold=3.0):
    """Figure 8-4: the float solution against the fixed one, per constellation.

    `modes` is a sequence of ``(label, sod, mag_float, mag_fixed, accepted)``:
    all four arrays share `sod`, `mag_fixed` is the fixed file's error **as
    written** (so it equals the float one on the epochs the ratio test
    rejected), and `accepted` is the mask selecting the epochs the fixed series
    is actually drawn on.

    The grey/red pairing is figure 8-1's: grey is the coarse solution the eye
    should discount, red the one under discussion. Here that is float against
    fixed rather than SPP against RTK, and the legends say so - the grammar is
    what carries over, not the identity of the two methods.

    **The window is figure 8-1's**, unchanged and shared by all three panels.
    That is what makes the two figures readable together: the grey band here is
    at the height of the red band there, and the gap between grey and red here is
    what fixing bought. Refitting it per constellation would erase the
    BDS-2/BDS-3 difference that section four is about.

    Drawing the fixed series only where the ratio test accepted is the point
    rather than a convenience. Interpolating it across a rejection, or falling
    back to the float value there, would show a series that no consumer sees:
    what a consumer gets on a rejected epoch is the float solution, and on
    BDS-2 that is metres, not millimetres.
    """
    modes = [(str(m[0]), np.asarray(m[1], dtype=float),
              np.asarray(m[2], dtype=float), np.asarray(m[3], dtype=float),
              np.asarray(m[4], dtype=bool)) for m in modes]
    n = max((int(sod.size) for _l, sod, *_r in modes), default=0)

    fig, axes = plt.subplots(3, 1, figsize=(11, 9.6), sharex=True)
    fig.suptitle(t(lang, "title_rtk_fix", rover=rover or "-", n=n,
                   thr="%.1f" % threshold), fontsize=12)

    for k, (ax, (label, sod, mag_f, mag_x, acc)) in enumerate(zip(axes, modes)):
        # A magnitude of exactly zero becomes a gap rather than a plotted
        # point; on the linear axis that gap sits on the bottom edge and is a
        # line-width wide. See fig_rtk_spp_vs_rtk_ts for the rule.
        grey = np.where(mag_f > 0.0, mag_f, np.nan)
        red = np.where(acc & (mag_x > 0.0), mag_x, np.nan)
        ax.plot(sod, _break_gaps(sod, grey), color=C_SPP, lw=0.6, alpha=0.85,
                zorder=2, label=t(lang, "legend_float"))
        ax.plot(sod, _break_gaps(sod, red), color=C_RTK, lw=0.6, alpha=0.95,
                zorder=3, label=t(lang, "legend_fixed"))

        n_acc = int(acc.sum())
        rate = (100.0 * n_acc / sod.size) if sod.size else 0.0
        frms = float(np.sqrt(np.nanmean(grey ** 2))) if sod.size else float("nan")
        xrms = (float(np.sqrt(np.nanmean(red ** 2))) if n_acc else float("nan"))
        worst = float(np.nanmax(red)) if n_acc else float("nan")
        ax.text(0.01, 0.97,
                t(lang, "annot_rtk_fix", mode=label, rate=rate, frms=frms,
                  xrms=xrms, worst=worst),
                transform=ax.transAxes, va="top", fontsize=8, color=INK,
                bbox=dict(facecolor="white", alpha=0.82, edgecolor="none", pad=2.0))
        ax.set_title(label, loc="left", fontsize=10, color=INK)
        if k == 0:
            ax.legend(fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    axes[1].set_ylabel(t(lang, "axis_rtk_mag_fix"), fontsize=10)
    for ax in axes:
        ax.set_ylim(bottom=0.0)
    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    _sod_axis(axes[-1], np.concatenate([s for _l, s, *_r in modes])
              if modes else np.asarray([], dtype=float))
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


def fig_rtk_ratio_hist(modes, out_png, rover="", lang="en", threshold=3.0):
    """Figure 8-5: where the ratio test falls, and how much it rejects.

    `modes` is a sequence of ``(label, ratio)``. Each constellation gets a
    stepped histogram of its ratio values as a fraction of its own epochs, so
    the three are comparable however many epochs they have, with the threshold
    drawn as a vertical line.

    The x axis is logarithmic because the ratios span five decades - the
    medians are in the hundreds and the smallest accepted value is just above
    the threshold - and because the question the figure answers is about the
    *tail* on the left: how much of the run sits below the line.

    A ratio of exactly 0 means no integer candidate existed at all (see
    ``ARLambda::resolve``), which is a different thing from a candidate that
    lost the test, so those epochs are counted separately in the annotation and
    left off the log axis rather than piled at an arbitrary left edge. On the
    chapter's runs there are none; a run where they appear is one where the
    covariance was unusable, which is worth seeing.
    """
    modes = [(str(m[0]), np.asarray(m[1], dtype=float)) for m in modes]
    series = [(label, r[r > 0.0], int((r <= 0.0).sum())) for label, r in modes]
    positive = np.concatenate([r for _l, r, _z in series if r.size]) \
        if any(r.size for _l, r, _z in series) else np.asarray([1.0])

    lo = max(0.5, float(positive.min()) * 0.8)
    hi = max(float(positive.max()) * 1.25, threshold * 2.0)
    edges = np.logspace(np.log10(lo), np.log10(hi), 41)

    fig, ax = plt.subplots(figsize=(11, 5.2))
    fig.suptitle(t(lang, "title_rtk_ratio", rover=rover or "-",
                   thr="%.1f" % threshold), fontsize=12)

    # One colour per constellation, from the same three categorical slots every
    # other chapter-8 figure uses, so a constellation keeps its colour across
    # 8-1, 8-2 and 8-5.
    colors = (C_E, C_N, C_U)
    for i, (label, r, n_zero) in enumerate(series):
        if r.size:
            ax.hist(r, bins=edges, histtype="step", lw=1.4,
                    color=colors[i % len(colors)],
                    density=False, weights=np.full(r.size, 100.0 / r.size),
                    label=t(lang, "annot_ratio", mode=label,
                            rate=100.0 * float((r > threshold).mean()),
                            n=int(r.size)))
        if n_zero:
            print(f"  note: {label}: {n_zero} epoch(s) with ratio 0 "
                  f"(no integer candidate) are not on the log axis")

    ax.axvline(threshold, color=C_SLIP, lw=1.2, ls="--", zorder=3)
    # To the RIGHT of the line and inside the axes. The threshold sits near the
    # left end of a five-decade axis, so a right-aligned label would hang out
    # over the y tick labels.
    ax.text(threshold * 1.15, ax.get_ylim()[1] * 0.97,
            t(lang, "annot_ratio_thr", thr="%.1f" % threshold),
            color=C_SLIP, fontsize=8, ha="left", va="top")
    ax.set_xscale("log")
    ax.set_xlabel(t(lang, "axis_ratio"), fontsize=10)
    ax.set_ylabel(t(lang, "axis_ratio_share"), fontsize=10)
    ax.legend(fontsize=8.5, loc="upper right", framealpha=0.9)
    style_ax(ax)
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    return _finish(fig, out_png)


def fig_rtk_lsq_vs_kalman_ts(panels, out_png, rover="", lang="en"):
    """Figure 8-6: the single-epoch solution against the Kalman filter.

    `panels` is a sequence of ``(label, sod_lsq, mag_lsq, sod_kal, mag_kal)``.
    Each series carries its **own** `sod`: the two are separate runs of separate
    files, and nothing in the format promises a shared epoch grid.

    The grey/red pairing is figures 8-1 and 8-4's: grey is the coarse solution
    the eye should discount, red the one under discussion. Here that is the
    single-epoch solution against the filter, and the legends say so - the
    grammar is what carries over, not the identity of the two methods.

    **The layout is figure 8-1's** - three panels, one per constellation, each
    scaled to its own data with the bottom pinned to zero - so the gap between
    the grey and red bands is read the same way in both figures. The gap is what
    the filter bought, the same quantity the section's tables report.

    Both series are annotated with the **3-D RMS**, not the median, because
    that is the statistic the tables use. A median would hide exactly what this
    figure exists to show: the filter's gain is not uniform in time. It starts
    from a large initial covariance and needs epochs to pull the state in, so
    BDS-3's first thousand epochs are barely better than the single-epoch
    solution while the second half of the run is tens of times better. A median
    over the whole run reports the converged value and makes the transient
    disappear - which is the reading the figure is here to prevent.
    """
    panels = [(str(p[0]), np.asarray(p[1], dtype=float),
               np.asarray(p[2], dtype=float), np.asarray(p[3], dtype=float),
               np.asarray(p[4], dtype=float)) for p in panels]
    n = max((int(p[1].size) for p in panels), default=0)

    fig, axes = plt.subplots(3, 1, figsize=(11, 9.6), sharex=True)
    fig.suptitle(t(lang, "title_rtk_kalman", rover=rover or "-", n=n),
                 fontsize=12)

    for k, (ax, (label, sod_l, mag_l, sod_k, mag_k)) in enumerate(
            zip(axes, panels)):
        # A magnitude of exactly zero becomes a gap rather than a plotted
        # point; on the linear axis that gap sits on the bottom edge. See
        # fig_rtk_spp_vs_rtk_ts for the rule. Each series is broken on its own
        # sod, not the other's.
        grey = np.where(mag_l > 0.0, mag_l, np.nan)
        red = np.where(mag_k > 0.0, mag_k, np.nan)
        ax.plot(sod_l, _break_gaps(sod_l, grey), color=C_SPP, lw=0.6,
                alpha=0.85, zorder=2, label=t(lang, "legend_lsq_float"))
        ax.plot(sod_k, _break_gaps(sod_k, red), color=C_RTK, lw=0.6,
                alpha=0.95, zorder=3, label=t(lang, "legend_kalman_float"))

        lrms = float(np.sqrt(np.nanmean(grey ** 2))) if sod_l.size else float("nan")
        krms = float(np.sqrt(np.nanmean(red ** 2))) if sod_k.size else float("nan")
        # Preformatted rather than a format spec in the string, so a NaN or a
        # zero cannot reach a `:.1f` inside _strings.t().
        gain = ("%.1f×" % (lrms / krms)) if krms > 0.0 else "-"
        ax.text(0.01, 0.97,
                t(lang, "annot_rtk_kalman", mode=label, lrms=lrms, krms=krms,
                  gain=gain, n1=int((mag_l > 1.0).sum()),
                  n2=int((mag_k > 1.0).sum())),
                transform=ax.transAxes, va="top", fontsize=8, color=INK,
                bbox=dict(facecolor="white", alpha=0.82, edgecolor="none",
                          pad=2.0))
        ax.set_title(label, loc="left", fontsize=10, color=INK)
        if k == 0:
            ax.legend(fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    axes[1].set_ylabel(t(lang, "axis_rtk_mag_kalman"), fontsize=10)
    for ax in axes:
        ax.set_ylim(bottom=0.0)
    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    sods = [s for _l, s_l, _a, s_k, _b in panels for s in (s_l, s_k)]
    _sod_axis(axes[-1], np.concatenate(sods) if sods
              else np.asarray([], dtype=float))
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


def fig_rtk_dd_cov_ratio_ts(panels, out_png, rover="", lang="en", threshold=3.0):
    """Figure 8-7: the ratio test statistic under both DD weightings.

    `panels` is a sequence of ``(label, sod_indep, ratio_indep, sod_cov,
    ratio_cov)``. Each series carries its **own** `sod` - the two runs are
    separate invocations and the file format promises no shared epoch grid.

    Grey is the run that weighted the double differences as independent (the
    default, and the byte-exact path); red the one that used their full
    covariance ``Q_DD = C*Q_SD*C^T``, which is chapter 8 exercise 3. Same
    grey/red grammar as figures 8-1 and 8-4: grey is what the eye discounts.

    **Why the ratio, of all quantities.** LAMBDA consumes the ambiguity
    covariance, so a different stochastic model reaches the *fix decision*
    through this statistic before it reaches anything else. The figure exists
    to answer one question - does knowing the double differences are correlated
    change which epochs pass - and on this dataset it does not: the medians
    shift by 5-12% and every ratio stays far above the threshold line, which is
    the flat dashed one at the bottom. The panels carry both medians and the
    count above the threshold so that the answer is a number, not an impression.

    A ratio of zero is not a small ratio: the diagnostics write 0 for "no
    integer candidate existed". Those epochs become NaN and leave a gap rather
    than being drawn as a ratio at the origin.

    Linear axes, one scale per panel, like the rest of the chapter's figures.
    """
    panels = [(str(p[0]), *(np.asarray(a, dtype=float) for a in p[1:]))
              for p in panels]
    n = max((int(p[1].size) for p in panels), default=0)

    fig, axes = plt.subplots(3, 1, figsize=(11, 9.6), sharex=True)
    fig.suptitle(t(lang, "title_rtk_dd_cov", rover=rover or "-", n=n),
                 fontsize=12)

    for k, (ax, (label, sod_i, r_i, sod_c, r_c)) in enumerate(
            zip(axes, panels)):
        gi = np.where(r_i > 0.0, r_i, np.nan)
        gc = np.where(r_c > 0.0, r_c, np.nan)
        # The two series overlap almost everywhere - that IS the result - so the
        # grey one is drawn wider and behind, and the red one thinner and on
        # top: with equal widths the red would simply hide the grey, and a
        # legend promising two series that only one of them delivers is worse
        # than no comparison at all.
        ax.plot(sod_i, _break_gaps(sod_i, gi), color=C_SPP, lw=0.9, alpha=0.9,
                zorder=2, label=t(lang, "legend_dd_indep"))
        ax.plot(sod_c, _break_gaps(sod_c, gc), color=C_RTK, lw=0.55, alpha=0.9,
                zorder=3, label=t(lang, "legend_dd_cov"))
        ax.axhline(threshold, color=ZERO, lw=0.9, ls="--", zorder=1)

        mi = float(np.nanmedian(gi)) if sod_i.size else float("nan")
        mc = float(np.nanmedian(gc)) if sod_c.size else float("nan")
        # Preformatted, so no NaN or zero reaches a format spec in the string.
        ax.text(0.01, 0.97,
                t(lang, "annot_rtk_dd_cov", mode=label,
                  m1=("%.0f" % mi) if np.isfinite(mi) else "-",
                  m2=("%.0f" % mc) if np.isfinite(mc) else "-",
                  n1=int(np.nansum(r_i >= threshold)),
                  n2=int(np.nansum(r_c >= threshold))),
                transform=ax.transAxes, va="top", fontsize=8, color=INK,
                bbox=dict(facecolor="white", alpha=0.82, edgecolor="none",
                          pad=2.0))
        ax.set_title(label, loc="left", fontsize=10, color=INK)
        if k == 0:
            ax.legend(fontsize=9, loc="upper right", framealpha=0.9)
        style_ax(ax)

    for ax in axes:
        ax.set_ylim(bottom=0.0)
    axes[1].set_ylabel(t(lang, "axis_rtk_ratio"), fontsize=10)
    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    sods = [s for _l, s_i, _a, s_c, _b in panels for s in (s_i, s_c)]
    _sod_axis(axes[-1], np.concatenate(sods) if sods
              else np.asarray([], dtype=float))
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


def fig_rtk_mixed_signed_ts(series, out_png, rover="", lang="en",
                            title_key=""):
    """Figures 8-8 and 8-9: the mixed solution's error, signed, one direction
    per panel.

    `series` is a sequence of ``(label, sod, enu, rms_3d, color)``, where `enu`
    holds the three signed components and `rms_3d` is the three-dimensional RMS
    the chapter's tables quote. The first caller compares the weighted mixed
    solution with the GPS-only and BDS-3-only ones (figure 8-8); the second
    compares the mixed default with the weighted mixed (figure 8-9).

    **Why not a 3-D magnitude.** |d| = sqrt(E^2+N^2+U^2) is non-negative by
    definition, so it cannot cross zero and cannot show which side an error lies
    on. Every point here is a signed component: above the zero line is east /
    north / up of the reference. The price is that the three directions have
    different ranges - on this run the vertical is 2-3 times the horizontal - so
    they get one panel each rather than one shared axis. That is the same split
    the 8-1/8-4/8-6 directional siblings use, for the same reason.

    The legend carries each series' **3-D RMS**, because that is what the
    tables and the section's conclusions are stated in; the text inside each
    panel carries the **directional RMS**, because that is what the panel shows.
    Neither is the bias: the signed curves are raw, and a reader who wants the
    mean offset can see it in the curve's position relative to the zero line.

    BDS-2-only is deliberately absent from figure 8-8: its float RMS is 1.1 m,
    an order of magnitude above the other two, and putting it on the same axis
    would press the comparison this figure exists for into the bottom edge. The
    section's table still carries it.
    """
    rows = []
    for label, sod, enu, rms_3d, color in series:
        e = np.asarray(enu, dtype=float)
        if e.shape[0] != 3:
            raise ValueError("enu must hold three components (E, N, U)")
        rows.append((str(label), np.asarray(sod, dtype=float), e,
                     float(rms_3d), color))
    n = max((int(sod.size) for _l, sod, _e, _r, _c in rows), default=0)

    fig, axes = plt.subplots(3, 1, figsize=(11, 9.6), sharex=True)
    fig.suptitle(t(lang, title_key, rover=rover or "-", n=n), fontsize=12)

    ykeys = ("axis_east_err", "axis_north_err", "axis_up_err")
    dirs = ("dir_e", "dir_n", "dir_u")
    for d, ax in enumerate(axes):
        values = []
        lines = []
        for label, sod, enu, rms_3d, color in rows:
            y = enu[d]
            values.append(y)
            ax.plot(sod, _break_gaps(sod, y), color=color, lw=0.7, alpha=0.9,
                    zorder=2,
                    label=(t(lang, "legend_mixed_3d", label=label, rms=rms_3d)
                           if d == 0 else None))
            lines.append("%s  %.3f m" % (label, float(np.sqrt(np.nanmean(y ** 2)))))
        ax.axhline(0.0, color=ZERO, lw=0.9, ls="--", zorder=1)

        finite = np.concatenate([np.abs(v[np.isfinite(v)]) for v in values])
        lim = float(np.max(finite)) * 1.08 if finite.size else 1.0
        lim = max(lim, 1e-3)
        ax.set_ylim(-lim, lim)

        ax.text(0.01, 0.97, "\n".join(lines), transform=ax.transAxes, va="top",
                fontsize=8, color=INK,
                bbox=dict(facecolor="white", alpha=0.82, edgecolor="none",
                          pad=2.0))
        ax.set_title(t(lang, dirs[d]), loc="left", fontsize=10, color=INK)
        ax.set_ylabel(t(lang, ykeys[d]), fontsize=10)
        if d == 0:
            ax.legend(fontsize=8.5, loc="upper right", framealpha=0.9)
        style_ax(ax)

    axes[-1].set_xlabel(t(lang, "axis_sod"), fontsize=10)
    sods = [sod for _l, sod, _e, _r, _c in rows]
    _sod_axis(axes[-1], np.concatenate(sods) if sods
              else np.asarray([], dtype=float))
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    return _finish(fig, out_png)


# ---------------------------------------------------------------------------
# the directional siblings (figures 8-1b/c/d, 8-4b/c/d and 8-6b/c/d)
# ---------------------------------------------------------------------------
#
# Figures 8-1, 8-4 and 8-6 each draw a whole series - for 8-1 all 7934 epochs of
# the chapter's run, two and a quarter hours of it - which is what lets the
# chapter's headline numbers be read off one picture. The three components of an
# ENU error are folded into one magnitude or one shared axis there, though, and
# that is what the siblings undo: **one direction per figure**, b/c/d standing
# for east, north and up. **Each of the three has not one sibling but three**,
# one per direction - nine figures in all.
#
# **Every panel still holds both series on one shared vertical axis**, and every
# figure still holds one constellation per panel - three panels, GPS then BDS-2
# then BDS-3. An earlier attempt gave each series a lane of its own, one above
# the other, and was rejected outright: "the whole point of a comparison figure
# is that both curves are in the same picture". Two lanes on two scales cannot
# be read against each other at all, and the distance between them - 5.9x to 32x,
# and a ratio of two things that do not measure the same quantity - invites a
# reading the chapter spends a section retracting. What the direction split buys
# instead is the y scale: an ENU triple spans an order of magnitude (BDS-2's
# error is nearly all in U), so one figure holding all three components would
# have to share one axis across ranges that differ that much. It also puts the
# Kalman filter's convergence where a reader can find it - it is a vertical
# phenomenon on this dataset, which is why 8-6b (east) shows nothing on BDS-2
# while 8-6d (up) shows the whole transient.
#
# **The two families take their span differently, and that is the one thing they
# do not share.** 8-1b/c/d draw *the same epochs as their parent*, through the
# parent's own ``_in_8_1_span``: they are figure 8-1 read one direction at a
# time, and what they add is the *sign* - the parent plots the magnitude of the
# 3-D error, which cannot carry one, and on this data the single-point solution's
# vertical offset does turn over: several metres either side of zero, on every
# constellation, while the RTK curve stays at centimetres.
# 8-4b/c/d and 8-6b/c/d take a short *window*
# instead, because their subject is invisible over a whole run: the Kalman filter
# settles inside its first few hundred epochs, which over 7934 of them is a
# pixel-wide sliver along the axis.
#
# A windowed family draws fewer epochs with a marker on every one of them, so a
# reader sees where the samples are rather than a continuous smear. The
# full-span family has no such need - a marker per point across 6556 points is
# the smear - and keeps its parent's thin unmarked line.
#
# These are separate figures rather than a parameterisation of their parents
# for the reason given beside ``_stacked_ts``: the parent figures' PNGs are
# pinned byte for byte, and an ``if window is not None`` threaded through their
# bodies is exactly the kind of change that moves a byte without moving the
# picture.
#
# Nothing here re-runs the solver. Both a window and 8-1's cap are slices of the
# outputs the chapter already has. ``apps/rtk.cpp --stop`` would give the same
# numbers at the same epochs - the Kalman filter is causal, so truncating the
# input is the same as truncating the output - but it would cost a second run
# tree to say what a slice of an array says for free.

#: The directions the directional figures are drawn one at a time, in the order
#: the sub-figure letters run: 8-1b/c/d are east, north and up. Public because
#: the CLI names one output file per direction from it, and a second copy of
#: these letters is a second thing to forget to change. `_ZOOM_LETTERS` is the
#: sub-figure letter, which is *not* the same as the direction's own letter
#: here - "e" for east is 8-1**b** - and is easy to get wrong.
ZOOM_DIRS = ("e", "n", "u")
_ZOOM_LETTERS = ("b", "c", "d")

#: Window and decimation for the two families that take one, as
#: ``(from_s, to_s, every_nth)`` seconds after the run's own first epoch. Chosen
#: by reading this run, and constants rather than options: a flag would invite a
#: figure whose caption no longer describes it. 8-1b/c/d are absent because they
#: take no window - they draw their parent's own span, through ``_in_8_1_span``.
#:
#: 8-4 takes a longer middle stretch than 8-6 so BDS-2's rejections show as
#: repeated gaps rather than one cluster. 8-6 takes the opening, where the
#: filter's transient lives - BDS-2, the slowest, has settled by about epoch
#: 250, so the transient fills five eighths of the width and the settled state
#: the rest, the settled part being the boring one.
_ZOOM_WINDOW_8_4 = (400.0, 2400.0, 16)
_ZOOM_WINDOW_8_6 = (0.0, 400.0, 4)

#: Floor for the vertical span of a direction panel, in metres. Only reached by
#: a perfectly flat series - on this chapter's runs the smallest window RMS is
#: millimetres - and it exists so that such a series still gets a panel with a
#: height rather than a zero-height axis to divide by.
_ZOOM_MIN_SPAN = 0.02


def _zoom_window(sod, first_sod, rel_lo, rel_hi, min_epochs=2):
    """Select one window out of a series, or report that it is not there.

    The window is given in **seconds after the run's own first epoch** rather
    than as an absolute ``sod``, so it lands in the same part of the pass on any
    dataset instead of on a specific clock time. Returns ``(mask, honoured)``:
    the mask selects the window, and `honoured` is False when the window is not
    a genuine sub-range of this series - which is the caller's cue to draw the
    whole run instead, with the parent figure's own axes.

    `honoured` goes False when the window runs *past the end* of the series, not
    merely when it selects nothing. The two cases differ: a series that ends
    before the window does is a run this window was never chosen for (the unit
    tests' synthetic run is two epochs long), whereas a series that spans the
    window but carries no epochs inside it is a real hole in the data, which a
    reader should see rather than have papered over. Either way the caller is
    told on stdout, and what it draws instead cannot be mistaken for a zoom -
    the x range is the whole pass and the annotation says so.
    """
    sod = np.asarray(sod, dtype=float)
    if not sod.size:
        return np.zeros(0, dtype=bool), False
    whole = np.ones(sod.size, dtype=bool)
    if not np.isfinite(sod.max()):
        return whole, False
    hi = float(first_sod) + float(rel_hi)
    if hi > float(sod.max()):
        return whole, False
    lo = float(first_sod) + float(rel_lo)
    mask = (sod >= lo) & (sod <= hi)
    if int(mask.sum()) < min_epochs:
        return whole, False
    return mask, True


def _zoom_epochs(drawn):
    """How many epochs are actually drawn, for the title."""
    return max((int(a.size) for a in drawn), default=0)


def _zoom_note(lang, fig, n, window):
    """Say on stdout that a zoom figure fell back to drawing the whole run.

    Loud on purpose. The fallback produces a legitimate-looking figure whose
    x axis is the whole pass, so the only things distinguishing it from a zoom
    are this line and an annotation that names no window - both of which a
    reader of the console or the caption will see, and neither of which a
    silent fallback would leave behind.
    """
    print("  " + t(lang, "note_rtk_zoom_fallback", fig=fig, n=n,
                   lo="%.0f" % window[0], hi="%.0f" % window[1]))


def _zoom_direction(direction):
    """The direction letter for a zoomed figure's index, or a loud failure.

    ``direction`` is 0, 1 or 2 for east, north and up. Raised rather than
    clamped: a caller that passed 3 would otherwise get east back and a figure
    labelled against its own filename.
    """
    try:
        return ZOOM_DIRS[int(direction)]
    except (IndexError, ValueError):
        raise ValueError("direction must be 0 (east), 1 (north) or 2 (up)")


def _zoom_indices(sods, window):
    """Each series' drawn epochs, or the whole of it when the window is absent.

    Returns ``(picks, honoured)``. `picks` is one index array per input series,
    holding the epochs inside the window, decimated to every ``window[2]``-th of
    them - the decimation is what lets every remaining point carry a marker and
    stay readable.

    The window is honoured only if **every** series contains it. A figure whose
    panels spanned different stretches of the run would not be one comparison,
    and 8-6's two series come from separate runs whose shared epoch grid is a
    measurement rather than a contract. When it is not honoured every series is
    drawn over its whole span and **undecimated** - the fallback, which
    :func:`_zoom_note` announces on stdout.

    The window is relative to the earliest first epoch of the lot, so all the
    panels sit in the same part of the pass.
    """
    first = min((float(np.asarray(s, dtype=float).min()) for s in sods
                 if np.size(s)), default=0.0)
    masks, honoured = [], True
    for s in sods:
        mask, ok = _zoom_window(s, first, window[0], window[1])
        masks.append(mask)
        honoured = honoured and ok
    step = int(window[2]) if honoured else 1
    return [np.nonzero(m)[0][::step] for m in masks], honoured


def _zoom_window_clause(lang, honoured, window):
    """The window, as a clause for a title - or "the whole run" on a fallback.

    The fallback redraws the entire series, so a title still naming the window
    would be the one lie the degraded path cannot afford.
    """
    if not honoured:
        return t(lang, "zoom_win_all")
    return t(lang, "zoom_win", lo="%.0f" % window[0], hi="%.0f" % window[1],
             step=int(window[2]))


def _zoom_stat(lang, y):
    """One curve's bias, sigma and RMS, printed at that curve's own scale.

    Two decimals is right for a metre-level single-point solution and useless
    for a fixed solution at 0.015 m, where it prints as 0.00 three times over.
    The numbers are formatted here rather than left to the string so the
    precision can follow the data, and so nothing but digits reaches a format
    spec.
    """
    y = np.asarray(y, dtype=float)
    y = y[np.isfinite(y)]
    if not y.size:
        return "-"
    rms = float(np.sqrt((y ** 2).mean()))
    dec = 2 if rms >= 0.1 else (3 if rms >= 0.01 else 4)
    fmt = "%+." + str(dec) + "f"
    mag = "%." + str(dec) + "f"
    return t(lang, "annot_rtk_zoom_stat", bias=fmt % y.mean(),
             std=mag % y.std(ddof=1), rms=mag % rms)


def _fit_zoom_dir(ax, *series):
    """Set a direction panel's y window so its two stat lines clear the data.

    The panel straddles zero - these are signed errors, not magnitudes - so the
    rule is about headroom rather than a floor: 6% of the span below the lowest
    value drawn, and the top placed so the highest sits at about 68% of the
    panel height. Without that the two corner lines are drawn across the curves
    they describe. Both series count towards the span because they share the
    axis, which is also why a panel can be much taller than the finer of its two
    curves needs: BDS-2's Kalman transient is 0.7 m in a panel the least-squares
    spikes stretch to 3 m.
    """
    vals = np.concatenate([np.asarray(s, dtype=float)[np.isfinite(s)]
                           for s in series if np.size(s)])
    if not vals.size:
        return
    lo, hi = float(vals.min()), float(vals.max())
    span = max(hi - lo, _ZOOM_MIN_SPAN)
    ax.set_ylim(lo - 0.06 * span, lo + 1.50 * span)


def _zoom_dir_panel(ax, sod_c, coarse, sod_f, focus, d, lang, names,
                    marked=True):
    """One panel of a directional figure: both series, one shared vertical axis.

    Blue is the coarse solution the eye should discount - SPP, the float
    solution, the single-epoch least squares - and red the one under discussion:
    the parent figures' grammar, with the coarse series recoloured from grey.
    See `C_ZOOM_COARSE`.

    Each series brings its own time base, because 8-6's two come from separate
    runs; 8-1 and 8-4 pass the same array twice. Each also prints its own
    bias/sigma/RMS in the top-left corner in its own colour. That is what
    carries the size comparison a shared linear axis cannot when the two differ
    by 50-60x, as they do on BDS-2: there the finer curve is a flat line along
    the coarser one's foot. `focus` may hold NaN where a fixed solution was
    rejected, and those epochs are gaps here exactly as in the parent figure.

    `marked` is the style of the two lines, and it is the one thing a windowed
    family and 8-1's full-span family need told apart: a decimated curve needs a
    marker on every survivor to be followable, and 8-1b/c/d - 6556 points, no
    decimation - need the parent's thin unmarked line instead. `ms` is inert
    without a marker, so only the format string and the width move.
    """
    ax.axhline(0.0, color=ZERO, lw=0.9, ls="--", zorder=1)
    fmt = "-o" if marked else "-"
    lw = 1.0 if marked else 0.7
    for sod, y, color, alpha, z, label in (
            (sod_c, coarse, C_ZOOM_COARSE, 0.90, 2, names[0]),
            (sod_f, focus, C_ZOOM_FOCUS, 0.95, 3, names[1])):
        ax.plot(sod, _break_gaps(sod, y), fmt, color=color, lw=lw, ms=3.4,
                alpha=alpha, zorder=z, label=label)
    ax.set_ylabel(t(lang, "axis_rtk_zoom_err",
                    dir=t(lang, "dir_" + d)), fontsize=9.5, color=INK)
    style_ax(ax)


def _zoom_finish(fig, axes, lang, drawn, out_png, xlabel=None):
    """X ticks, the shared x label and the save - the tail all three share.

    The y labels are the panels' own business: every panel of a directional
    figure names the direction it is about, because a figure's three panels are
    its three constellations, not its three directions. `xlabel` is that same
    business for the x axis: 8-1's family stops before the run does and has to
    say where, while the six windowed figures draw a stretch the run really
    contains and keep the plain label.
    """
    axes[-1].set_xlabel(xlabel if xlabel is not None else t(lang, "axis_sod"),
                        fontsize=10)
    flat = [a for a in drawn if a.size]
    _sod_axis(axes[-1], np.concatenate(flat) if flat
              else np.asarray([], dtype=float))
    # 0.93 rather than the usual 0.95: these titles run to two lines. One line
    # cannot hold the window, the rover's file name and the drawn epoch count
    # together with the comparison and the direction - measured, a single line
    # of the Chinese title runs 40% wider than the 11-inch figure and is clipped
    # at both ends, which is worse than long because it hides which direction
    # the figure is about.
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    return _finish(fig, out_png)


def _zoom_dir_figure(panels, out_png, rover, lang, d, title_key, names,
                     window=None, fallback_fig=None, sod_max=None):
    """The body all three directional families share: span, panels, finish.

    `panels` is one ``(label, sod_coarse, coarse, sod_focus, focus)`` per
    constellation, in the order they are drawn; `names` is the two legend
    labels, coarse first. 8-1 and 8-4 draw both of their series from one run and
    pass the same ``sod`` twice; 8-6 passes two, because its two runs share an
    epoch grid by measurement rather than by contract.

    **Exactly one of `window` and `sod_max` has to be given, and which one it is
    is the whole of what separates the two kinds of family.** `window` is a
    stretch of the run, as ``(from_s, to_s, every_nth)`` seconds after the first
    epoch - what 8-4b/c/d and 8-6b/c/d take: decimated, every drawn point
    marked, falling back to the whole run (and saying so on stdout, against
    `fallback_fig`) when the run is shorter than the window. `sod_max` is a cap
    on **absolute** sod - what 8-1b/c/d take, so that they cover the epochs
    figure 8-1 draws and no others: no decimation, no markers, no fallback (a
    run shorter than the cap is simply a run wholly inside it), and both the
    title and the x label name the cap, because a curve that stops before the
    run does otherwise reads as a shorter run.

    The layout, the shared axis and the per-curve statistics are the same in all
    three families - only their data, their spans, their titles and what the two
    colours are called differ.
    """
    if (window is None) == (sod_max is None):
        raise ValueError("give exactly one of window= (a windowed family) or "
                         "sod_max= (8-1's full-span family)")

    sods = [s for p in panels for s in (p[1], p[3])]
    if sod_max is None:
        picks, honoured = _zoom_indices(sods, window)
        if not honoured:
            _zoom_note(lang, fallback_fig, _zoom_epochs(sods),
                       (window[0], window[1]))
        clause = _zoom_window_clause(lang, honoured, window)
        xlabel, marked = None, True
    else:
        # `_in_8_1_span` and not `s <= sod_max` written out here: the parent
        # figure clips through that same function, and these three are meant to
        # be the parent read one direction at a time. See it for why.
        picks = [np.flatnonzero(_in_8_1_span(s, sod_max)) for s in sods]
        clause = t(lang, "zoom_win_span", to="%d" % int(sod_max))
        xlabel = t(lang, "axis_sod_to", to="%d" % int(sod_max))
        marked = False

    # Both pickers walk their input in order, so the picks come back as one
    # (coarse, focus) pair per panel.
    pairs = list(zip(picks[0::2], picks[1::2]))
    drawn = [p[1][i] for p, (i, _j) in zip(panels, pairs)]
    drawn += [p[3][j] for p, (_i, j) in zip(panels, pairs)]

    fig, axes = plt.subplots(3, 1, figsize=(11, 9.6), sharex=True)
    fig.suptitle(t(lang, title_key, rover=rover or "-",
                   dir=t(lang, "dir_" + d), win=clause,
                   n=_zoom_epochs(drawn)), fontsize=12)

    for ax, (label, sod_c, coarse, sod_f, focus), (i, j) in zip(axes, panels,
                                                                pairs):
        _zoom_dir_panel(ax, sod_c[i], coarse[i], sod_f[j], focus[j], d, lang,
                        names, marked=marked)
        for frac, y, color, name in ((0.96, coarse[i], C_ZOOM_COARSE, names[0]),
                                     (0.78, focus[j], C_ZOOM_FOCUS, names[1])):
            ax.text(0.012, frac, "%s  %s" % (name, _zoom_stat(lang, y)),
                    transform=ax.transAxes, va="top", fontsize=8, color=color,
                    bbox=dict(facecolor="white", alpha=0.85, edgecolor="none",
                              pad=1.4))
        _fit_zoom_dir(ax, coarse[i], focus[j])
        ax.set_title(label, loc="left", fontsize=10, color=INK)
        style_ax(ax)

    axes[0].legend(fontsize=9, loc="upper right", framealpha=0.9)
    return _zoom_finish(fig, axes, lang, drawn, out_png, xlabel=xlabel)


def fig_rtk_spp_vs_rtk_dir_ts(modes, out_png, rover="", lang="en", direction=0):
    """Figures 8-1b/c/d: figure 8-1, one direction at a time.

    `modes` is figure 8-1's own input - ``(label, sod, stats)`` - and every
    number on the figure comes out of that same dict, so this figure and the
    tables cannot drift apart. `direction` is 0, 1 or 2 for east (8-1b), north
    (8-1c) and up (8-1d).

    **The same epochs as the parent, not a window into them.** Every series is
    drawn at every epoch figure 8-1 draws, through the parent's own
    ``_in_8_1_span``. Drawing them over one short window was tried first and is
    what this replaces: it made these three look like the six windowed siblings
    of 8-4 and 8-6, when their job is the opposite one - they take the parent's
    span and change only what is *read off* it.

    **Signed errors towards the reference, not distances from it.** A curve
    above the dashed zero line is north of the reference, not "0.4 m out". The
    parent plots the magnitude of the whole 3-D error, which cannot carry a sign
    at all, and the sign is the whole of what this figure adds - and it moves.
    Over the span drawn here the single-point solution's vertical mean swings
    across zero: GPS -1.4 m over the opening, -2.6 m around sod 28000 and
    +2.1 m over the closing stretch; BDS-3 +1.1, -0.7, then +3.5. A U RMS of
    2.18 / 10.99 / 2.14 m says none of that.

    **The two series share one axis, undecimated, both drawn as thin unmarked
    lines** - the parent's own style, and the only one that reads at 6556 points
    a series. What a shared axis cannot carry is how far apart the two are,
    5.9-32x on this data; each curve prints its own bias/sigma/RMS in its own
    corner instead, and the parent figure and section four's table keep the
    ratio. Two lanes on two scales, one per series, were tried and rejected:
    that is no comparison at all.

    The x axis stops at 8-1's cap and the label says so - the run carries on
    past it, and a curve that stopped short without saying so would read as a
    shorter run.
    """
    modes = [(str(m[0]), np.asarray(m[1], dtype=float), m[2]) for m in modes]
    di = int(direction)
    d = _zoom_direction(di)
    panels = [(label, sod, np.asarray(stats["spp_enu"], dtype=float)[di], sod,
               np.asarray(stats["rtk_enu"], dtype=float)[di])
              for label, sod, stats in modes]
    return _zoom_dir_figure(
        panels, out_png, rover, lang, d, "title_rtk_spp_zoom",
        (t(lang, "legend_spp"), t(lang, "legend_rtk")),
        sod_max=_WINDOW_8_1_SOD_MAX)


def fig_rtk_float_vs_fixed_zoom_ts(series, out_png, rover="", lang="en",
                                   direction=0):
    """Figures 8-4b/c/d: figure 8-4 over one window, one direction at a time.

    `series` is ``(label, sod, enu_float, enu_fixed, accepted)`` per
    constellation - figure 8-4's own input by component rather than by magnitude
    - and the fixed series is still drawn only where the ratio test accepted it,
    so a rejection is a hole here exactly as it is in the parent. `direction` is
    0, 1 or 2 for east (8-4b), north (8-4c) and up (8-4d).

    **This is where the fixed solution's own shape gets drawn for the first
    time.** On the parent figure the two series share an axis fitted to the
    float solution, so the fixed one sits at 0.2-0.5% of the panel height on
    BDS-2 - thinner than the axis line itself - and has been asserted rather
    than shown. Here the same axis still holds both, but the direction split has
    already taken an order of magnitude out of the vertical range the components
    need, so the fixed curve has room to be read; where the size difference is
    still too large for one axis, each curve prints its own RMS in its own
    corner.

    The window is deliberately the widest of the two families that take one
    (400-2400 s), so
    that BDS-2's rejections show as repeated gaps rather than a single cluster -
    which is a thing the parent cannot show either.
    """
    di = int(direction)
    d = _zoom_direction(di)
    panels = []
    for label, sod, enu_f, enu_x, acc in series:
        sod = np.asarray(sod, dtype=float)
        focus = np.asarray(enu_x, dtype=float)[di]
        panels.append((str(label), sod, np.asarray(enu_f, dtype=float)[di], sod,
                       np.where(np.asarray(acc, dtype=bool), focus, np.nan)))
    return _zoom_dir_figure(
        panels, out_png, rover, lang, d, "title_rtk_fix_zoom",
        (t(lang, "legend_float"), t(lang, "legend_fixed")),
        window=_ZOOM_WINDOW_8_4, fallback_fig="8-4" + _ZOOM_LETTERS[di])


def fig_rtk_lsq_vs_kalman_zoom_ts(panels, out_png, rover="", lang="en",
                                  direction=0):
    """Figures 8-6b/c/d: figure 8-6 over its opening window, one direction.

    `panels` is figure 8-6's own input - ``(label, sod_lsq, enu_lsq, sod_kal,
    enu_kal)`` - with both series carrying their own ``sod``. Both are windowed
    against **one** first epoch so the two lines span the same interval, and the
    window is the opening 400 s, because that is where the filter's transient
    lives. `direction` is 0, 1 or 2 for east (8-6b), north (8-6c) and up (8-6d).

    **The transient is the whole reason this family exists, and on this dataset
    it is a vertical phenomenon - which is why it takes three figures to say
    so.** Over the first ten epochs the filter's error on BDS-2 is +0.68 m in U
    and -0.18 m in N, but only -0.04 m in east, which is the same order as its
    own settled noise (0.06 m RMS in this window): the east panel has nothing to
    show, and a reader who looked only at 8-6b would conclude the filter had no
    transient at all. The cause is geometry - BDS-2 is an IGSO constellation,
    the weakest of the three in vertical observability, so the direction the
    filter takes longest to pull in is the one its initial error grows in. A
    figure holding all three components on one axis could not show this either,
    because BDS-2's own components span a factor of 17.

    Both series share each panel's axis, decimated to every 4th epoch with every
    marker drawn, so the decay is a shape rather than a sliver. Where a shared
    axis cannot carry the size difference, each curve states its own RMS in its
    own corner.
    """
    di = int(direction)
    d = _zoom_direction(di)
    rows = []
    for label, sod_l, enu_l, sod_k, enu_k in panels:
        rows.append((str(label), np.asarray(sod_l, dtype=float),
                     np.asarray(enu_l, dtype=float)[di],
                     np.asarray(sod_k, dtype=float),
                     np.asarray(enu_k, dtype=float)[di]))
    return _zoom_dir_figure(
        rows, out_png, rover, lang, d, "title_rtk_kalman_zoom",
        (t(lang, "legend_lsq_float"), t(lang, "legend_kalman_float")),
        window=_ZOOM_WINDOW_8_6, fallback_fig="8-6" + _ZOOM_LETTERS[di])

