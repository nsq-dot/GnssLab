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
    "C_E", "C_N", "C_U", "C_SLIP", "INK", "GRIDC", "ZERO",
    "fig_pos_error_enu_ts",
    "fig_pos_horizontal",
    "fig_velocity_enu_ts",
    "fig_clock_drift",
    "fig_slip_rate_bars",
    "fig_slip_series",
    "fig_state_composition",
    "fig_injection_outcomes",
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
