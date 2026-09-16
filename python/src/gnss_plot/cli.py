# -*- coding: utf-8 -*-
"""The ``gnss`` command-line interface.

This is the front door to the whole system. It orchestrates: the C++ engine does
the numerics and writes text output, this layer reads that output and turns it
into an accuracy report and figures. The interface between them is a pair of
files, so neither side needs to know about the other's internals.

Subcommands::

    gnss build     configure and build the C++ targets
    gnss spp       run the solver on a RINEX observation file
    gnss plot      analyse and plot existing solver output
    gnss run       spp + plot
    gnss demo      run + plot on the bundled sample dataset, zero arguments
    gnss cs-plot   plot the chapter-7 cycle-slip detector output
    gnss app       run one of the auxiliary C++ programs
    gnss ex        run one of the teaching examples

``gnss demo`` is the one to try first: it needs no arguments and no downloaded
data, and writes a report plus four figures.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

from . import _find
from ._strings import LANGS, normalise_lang, t

__all__ = ["main"]

# Auxiliary C++ programs, mapped to the subcommand name a user would type.
AUX_APPS = {
    "bds-eph": "bds_eph",
    "bds-gps-diff": "bds_gps_diff",
    "read-rinex": "read_rinex",
    "system-bias": "system_bias",
    "cs-detect-mw": "cs_detect_mw",
    "cs-detect-gf": "cs_detect_gf",
    "matrix": "matrix_calculator",
}

TEACHING_EXAMPLES = [
    "parse_opt",
    "parse_config",
    "gpst_to_utc",
    "bdweek_to_commontime",
    "jd2020_test",
    "ecef_enu_test",
]


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _fail(msg: str, code: int = 1):
    print(f"error: {msg}", file=sys.stderr)
    return code


def _force_utf8_stdout() -> None:
    """Make stdout/stderr UTF-8.

    The console report contains sigma, +/-, and (in Chinese) CJK characters.
    On a Windows console defaulting to a legacy code page these raise
    UnicodeEncodeError and abort the report partway through.
    """
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8")
        except (AttributeError, ValueError):
            pass


def _resolve_out_dir(root: str, out_dir: str | None) -> str:
    if not out_dir:
        return os.path.join(root, "output")
    return out_dir if os.path.isabs(out_dir) else os.path.join(root, out_dir)


def _paths_for(root, out_dir, rnx_name, mode):
    """Locate the solver output, preferring the manifest over the name convention."""
    from .io import output_paths, read_manifest, find_manifest

    guess = output_paths(out_dir, rnx_name, mode)
    mf = find_manifest(out_dir, rnx_name)
    if mf:
        data = read_manifest(mf)
        if data:
            spp = data.get("sppOut")
            vel = data.get("posVelOut")
            spp = spp if spp and os.path.isabs(spp) else os.path.join(root, spp or "")
            vel = vel if vel and os.path.isabs(vel) else os.path.join(root, vel or "")
            if os.path.isfile(spp) and os.path.isfile(vel):
                return spp, vel, data
    return guess["spp"], guess["vel"], None


# ---------------------------------------------------------------------------
# subcommands
# ---------------------------------------------------------------------------

def cmd_build(args) -> int:
    root = _find.project_root()
    cmake = _find.find_cmake()
    if not cmake:
        return _fail(
            "CMake not found.\n"
            "  Install it (https://cmake.org/download/, `apt install cmake`, or a\n"
            "  package manager), or point $CMAKE_EXE at your cmake executable."
        )

    build_dir = args.build_dir or os.path.join(root, "build")
    os.makedirs(build_dir, exist_ok=True)

    cfg = [cmake, "-S", root, "-B", build_dir,
           f"-DCMAKE_BUILD_TYPE={args.build_type}"]

    ninja = _find.find_ninja()
    if args.generator:
        cfg += ["-G", args.generator]
    elif ninja:
        cfg += ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    # Otherwise let CMake choose (Unix Makefiles on Linux, MSVC on Windows).

    print("configuring:", " ".join(cfg))
    r = subprocess.run(cfg)
    if r.returncode != 0:
        return _fail("CMake configure failed.")

    build = [cmake, "--build", build_dir, "-j", str(args.jobs)]
    print("building:", " ".join(build))
    r = subprocess.run(build)
    if r.returncode != 0:
        return _fail("build failed.")

    binpath = os.path.join(build_dir, "bin")
    n = len([f for f in os.listdir(binpath)]) if os.path.isdir(binpath) else 0
    print(f"built {n} executables into {binpath}")
    return 0


def cmd_spp(args) -> int:
    root = _find.project_root()
    exe = _find.find_binary("spp_if", root)
    if not exe:
        return _fail("spp_if not found - run 'gnss build' first.")

    cmd = [exe, args.config]
    if args.obs:
        cmd += ["--obs", args.obs]
    if args.nav:
        cmd += ["--nav", args.nav]
    if args.out_dir:
        cmd += ["--out-dir", args.out_dir]
    if args.stop:
        cmd += ["--stop", args.stop]
    if args.mode:
        cmd += ["--mode", args.mode]
    if args.verbose:
        cmd += ["--verbose"]

    print("running:", " ".join(cmd))
    return subprocess.run(cmd, cwd=root).returncode


def cmd_plot(args) -> int:
    from . import figures
    from .io import load_pos_vel, load_spp_xyz, read_approx_position

    root = _find.project_root()
    lang = normalise_lang(args.lang)
    figures.configure(lang=lang, backend="Agg")

    out_dir = _resolve_out_dir(root, args.out_dir)
    rnx_name = args.rinex
    if not rnx_name:
        return _fail("--rinex is required (name of the observation file, e.g. "
                     "data/WUH200CHN_....rnx), or use 'gnss demo'.")

    rnx_path = rnx_name if os.path.isabs(rnx_name) else os.path.join(root, rnx_name)

    spp_file = args.spp_out
    vel_file = args.pos_vel
    if not (spp_file and vel_file):
        a, b, _ = _paths_for(root, out_dir, os.path.basename(rnx_path), args.mode)
        spp_file = spp_file or a
        vel_file = vel_file or b

    for label, p in (("position output", spp_file), ("velocity output", vel_file),
                     ("RINEX observation", rnx_path)):
        if not os.path.isfile(p):
            return _fail(f"{label} not found: {p}\n"
                         "  Run 'gnss spp' first, or 'gnss demo' for the bundled sample.")

    ref_xyz = read_approx_position(rnx_path, lang=lang)
    sod_p, X, Y, Z = load_spp_xyz(spp_file)
    sod_v, _x, _y, _z, v_e, v_n, v_u, clkdot = load_pos_vel(vel_file)

    if v_e.size == 0:
        return _fail(f"no data rows parsed from {vel_file}")

    if args.report:
        from .stats import report_spp_vel
        from .coords import enu_position_error
        d_e, d_n, d_u = enu_position_error(X, Y, Z, ref_xyz)
        report_spp_vel(X, Y, Z, d_e, d_n, d_u, v_e, v_n, v_u, clkdot, ref_xyz,
                       station_label=args.station, mode_label=args.mode, lang=lang)

    if args.no_figures:
        return 0

    from .coords import enu_position_error
    d_e, d_n, d_u = enu_position_error(X, Y, Z, ref_xyz)

    png_dir = args.png_dir or out_dir
    os.makedirs(png_dir, exist_ok=True)
    figs = [
        figures.fig_pos_error_enu_ts(sod_p, d_e, d_n, d_u,
                                     os.path.join(png_dir, "vis_pos_error_enu_ts.png"),
                                     mode_label=args.mode, lang=lang),
        figures.fig_pos_horizontal(d_e, d_n,
                                   os.path.join(png_dir, "vis_pos_horizontal.png"),
                                   lang=lang),
        figures.fig_velocity_enu_ts(sod_v, v_e, v_n, v_u,
                                    os.path.join(png_dir, "vis_velocity_enu_ts.png"),
                                    mode_label=args.mode, lang=lang),
        figures.fig_clock_drift(sod_v, clkdot,
                                os.path.join(png_dir, "vis_clock_drift.png"),
                                mode_label=args.mode, lang=lang),
    ]
    print()
    print(t(lang, "saved_to", dir=png_dir))
    for f in figs:
        print("  -", f)
    return 0


def cmd_run(args) -> int:
    rc = cmd_spp(args)
    if rc != 0:
        return rc
    return cmd_plot(args)


def cmd_demo(args) -> int:
    """Run end to end on the bundled sample - no arguments, no downloads."""
    root = _find.project_root()
    sample = os.path.join(root, "data", "sample")

    obs = os.path.join(sample, "WUH200CHN_R_20250010000_01D_30S_MO.rnx")
    nav = os.path.join(sample, "BRDC00IGS_R_20250010000_01D_MN.rnx")

    if not os.path.isfile(obs) or not os.path.isfile(nav):
        return _fail(
            f"sample dataset missing from {sample}\n"
            "  Generate it with `python scripts/make_sample_data.py`, or download\n"
            "  the full dataset - see data/README.md."
        )

    # The manifest keys off the file name, and the sample shares the name of the
    # full dataset, so give the demo its own output directory to avoid mixing it
    # up with a real run.
    out_dir = os.path.join(root, "output", "demo")
    os.makedirs(out_dir, exist_ok=True)

    # Build the full argument set explicitly rather than reusing the demo
    # parser's namespace: cmd_plot and cmd_spp expect options that the `demo`
    # subcommand deliberately does not expose (it is meant to take no
    # arguments), and a hand-written namespace keeps the two in step.
    from argparse import Namespace

    print("Running the bundled sample dataset (station WUH2, 63 epochs).\n")

    solver_args = Namespace(
        config=os.path.join(root, "config", "spp.ini"),
        obs=obs, nav=nav, out_dir=out_dir,
        stop=None, mode="DUAL_IF", verbose=False,
    )
    rc = cmd_spp(solver_args)
    if rc != 0:
        return rc

    print()
    plot_args = Namespace(
        rinex=obs, spp_out=None, pos_vel=None,
        out_dir=out_dir,
        png_dir=(os.path.join(root, "docs", "figures") if args.docs_figures else out_dir),
        mode="DUAL_IF",
        station=args.station,
        lang=args.lang,
        no_figures=False,
        report=True,
    )
    return cmd_plot(plot_args)


# Where the chapter-7 runs live under `--cs-dir`. These names are not produced by
# the C++ programs - `--out-dir` writes whatever it is told - they are the layout
# docs/cycle-slip-gf.md's reproduction section prescribes. Encoding them as
# defaults, with an override each, is honest about that; globbing the directory
# would not be, because nothing in the metadata pairs a run with its manifest and
# both manifests sit side by side.
_CS_LAYOUT = {
    "run_1hz": ("zero-1hz",),
    "run_30s": ("sample-30s",),
    "inject_1hz": ("injected", "run"),
    "inject_30s": ("injected", "run-30s"),
    "manifest_1hz": ("injected", "oem719-injected.obs.slips.csv"),
    "manifest_30s": ("injected", "wuh2-injected.rnx.slips.csv"),
    # The MW runs of the same four datasets. They live in their own directories
    # rather than beside the GF ones: `<sat>.mw` and `<sat>.gf.diff` are read by
    # the same loader, so a mixed directory would still parse, but nothing would
    # then distinguish "no MW run" from "a run that reported nothing".
    "run_1hz_mw": ("zero-1hz-mw",),
    "run_30s_mw": ("sample-30s-mw",),
    "inject_1hz_mw": ("injected", "run-mw"),
    "inject_30s_mw": ("injected", "run-30s-mw"),
}

#: The injected cases that demonstrate the complementary blind spots, one
#: figure each: (manifest label, output PNG, title string key). Both read the
#: injected 1 Hz run, so both are skipped on a fresh clone - the 1 Hz zero
#: baseline is not committed. The pair is the point: GF is blind to (77, 60)
#: and MW is blind to (1, 1), and neither figure alone shows that.
_NULLSPACE_FIGURES = (
    ("gf-null-space", "vis_cs_gf_nullspace.png", "title_nullspace"),
    ("min-cycle-pair", "vis_cs_mw_nullspace.png", "title_nullspace_mw"),
)


def _warn(msg: str) -> None:
    print(f"warning: {msg}", file=sys.stderr)


def _cs_run_summary(directory: str):
    """Load one clean detector run into the shape the figures want.

    Returns ``(run, states, n_epochs)`` or None if either summary is missing.
    The per-satellite columns are SUMMED rather than read from the TOTAL row:
    that row carries only nEpochs, nTested and nSlip, and leaves the state counts
    empty.
    """
    from .io import load_gf_summary

    diff_path = os.path.join(directory, "summary.gf.diff.csv")
    poly_path = os.path.join(directory, "summary.gf.poly.csv")
    if not (os.path.isfile(diff_path) and os.path.isfile(poly_path)):
        return None

    sat, dcols, _dmeta, dtotal = load_gf_summary(diff_path)
    _sat2, pcols, _pmeta, ptotal = load_gf_summary(poly_path)

    def side(cols, total):
        return {"rate": cols["slipRate"], "nSlip": total["nSlip"],
                "nTested": total["nTested"]}

    states = {
        "diff": [float(dcols[k].sum()) for k in
                 ("nOk", "nSlip", "nInit", "nGap", "nWarmup")],
        "poly": [float(pcols[k].sum()) for k in
                 ("nOk", "nSlip", "nInit", "nGap", "nWarmup")],
    }
    return ({"sat": sat, "diff": side(dcols, dtotal), "poly": side(pcols, ptotal)},
            states)


def _cs_mw_run(directory: str, label: str):
    """Load one MW run into the shape ``fig_slip_rate_bars`` wants.

    MW's summary carries different column names from the GF ones - nFlagged and
    nSlipRate rather than nSlip and slipRate - which is why this is not folded
    into ``_cs_run_summary``; the reader itself needs no special case, and its
    TOTAL row is short, which it already knows about.
    """
    from .io import load_gf_summary

    path = os.path.join(directory, "summary.mw.csv")
    if not os.path.isfile(path):
        return None
    sat, cols, _meta, total = load_gf_summary(path)
    return {"label": label, "sat": sat,
            "mw": {"rate": cols["nSlipRate"],
                   "nSlip": total["nFlagged"], "nTested": total["nEpochs"]}}


def _cs_nullspace_panel(paths, case="gf-null-space"):
    """Everything a null-space figure needs, read from the injected 1 Hz runs.

    Returns ``(gf_view, mw_view, sat, dn1, dn2, sod)``, or None when the runs or
    the manifest are absent - which is the normal case on a fresh clone, since the
    1 Hz zero baseline is not committed.

    `case` picks which injected slip to draw: ``gf-null-space`` for (77, 60),
    where GF is blind, or ``min-cycle-pair`` for (1, 1), where MW is. Both go
    through the same reader, because the figure shows the same two panels either
    way and only the roles are reversed.

    The satellite and the epoch come out of the ground-truth manifest rather than
    being written into the figure, so re-running the injector with a different
    plan moves the figure with it instead of leaving it pointing at an epoch
    where nothing was injected.
    """
    from . import cycleslip, io

    if not os.path.isfile(paths["manifest_1hz"]):
        return None
    sat, _year, _doy, sod, label, dn1, dn2, _edli, _expect = io.load_slip_manifest(
        paths["manifest_1hz"])
    labels = [str(x) for x in label]
    if case not in labels:
        return None
    i = labels.index(case)
    target, epoch = str(sat[i]), float(sod[i])

    gf = os.path.join(paths["inject_1hz"], "%s.gf.diff" % target)
    mw = os.path.join(paths["inject_1hz_mw"], "%s.mw" % target)
    if not (os.path.isfile(gf) and os.path.isfile(mw)):
        return None
    return (cycleslip.series_view(gf, "diff"), cycleslip.series_view(mw, "mw"),
            target, int(dn1[i]), int(dn2[i]), epoch)


def _cs_injection_panel(label, manifest, run_dir, mw_run_dir=None):
    """Score one injected run with each detector into a figure panel.

    MW joins only when its own run is there. It lives in a separate directory,
    and a checkout that has the GF runs but not the MW ones should still get the
    two-detector panel rather than no panel at all.
    """
    from . import cycleslip

    if not (os.path.isfile(manifest)
            and cycleslip.detector_files(run_dir, "diff")
            and cycleslip.detector_files(run_dir, "poly")):
        return None

    modes = ["diff", "poly"]
    if mw_run_dir and cycleslip.detector_files(mw_run_dir, "mw"):
        modes.append("mw")

    scores = {}
    for m in modes:
        d = mw_run_dir if m == "mw" else run_dir
        scores[m] = cycleslip.score_injection(
            manifest, cycleslip.load_detector_run(d, m))
    ref = scores["diff"]["cases"]
    return {
        "title": label,
        "labels": [c["label"] for c in ref],
        "expect": [c["expect"] for c in ref],
        "status": {m: [c["status"] for c in scores[m]["cases"]] for m in modes},
        "score": {m: {"tp": scores[m]["tp"],
                      "n": scores[m]["tp"] + scores[m]["fn"],
                      "rate": scores[m]["rate"] or 0.0} for m in modes},
    }


def cmd_cs_plot(args) -> int:
    """Plot the chapter-7 cycle-slip detector output.

    Reads existing detector output; it does not run the detectors. Missing runs
    are skipped with a warning rather than failing the command - the 1 Hz zero
    baseline is not committed, so on a fresh clone half of the experiment cannot
    exist, and failing hard there would make the command useless exactly where
    someone is most likely to try it. Producing nothing at all IS an error.
    """
    from . import cycleslip, figures

    root = _find.project_root()
    lang = normalise_lang(args.lang)
    figures.configure(lang=lang, backend="Agg")

    # Default to output/cs, which is where config/cs.ini points - not output/,
    # which is what _resolve_out_dir falls back to for the solver.
    cs_dir = _resolve_out_dir(root, args.cs_dir or os.path.join("output", "cs"))

    def locate(key):
        override = getattr(args, key, None)
        if override:
            return override if os.path.isabs(override) else os.path.join(root, override)
        return os.path.join(cs_dir, *_CS_LAYOUT[key])

    paths = {key: locate(key) for key in _CS_LAYOUT}

    png_dir = (os.path.join(root, "docs", "figures") if args.docs_figures
               else (args.png_dir or os.path.join(cs_dir, "figures")))
    os.makedirs(png_dir, exist_ok=True)

    figs = []

    # --- §一: what the repaired detector reports, over a whole pass ---
    # One satellite's MW combination on the 1 Hz zero baseline, first because it
    # is §一 of the chapter.
    #
    # G08 out of the 26 satellites the 1 Hz run covers, and chosen by reading
    # them rather than by counting flags: on this dataset the combination is flat
    # for every satellite that tracks well, so the flags are what varies. Most
    # satellites spend the whole pass at 2 (the arc-start flag alone), which shows
    # a clean line and no detection; the noisier ones (G14, C03) wander by tens of
    # cycles and flag hundreds of epochs in a block, which reads as a smear rather
    # than as slips. G08 sits between: p95-p5 of its 6544 values span 0.33 cycles,
    # and its 25 flags are spread along the hour - one 79-cycle step in the middle and
    # single epochs either side of it.
    #
    # Unlike the RINEX-header figure this replaces, it needs a detector run, which
    # is now true of every figure here: the "nothing was produced" check below no
    # longer excuses any of them.
    mw_1hz = os.path.join(paths["run_1hz_mw"], "G08.mw")
    if os.path.isfile(mw_1hz):
        figs.append(figures.fig_mw_series(
            cycleslip.series_view(mw_1hz, "mw"),
            os.path.join(png_dir, "vis_cs_mw_series_G08.png"),
            sat="G08", run_label="zero-1hz-mw", lang=lang, deviation=False))
    else:
        _warn(f"skipping the §一 MW series for G08: {mw_1hz} not found")

    # --- slip rate per satellite, and the state composition ---
    runs, bars = [], []
    for key, label in (("run_1hz", "1 Hz zero baseline"), ("run_30s", "30 s sample")):
        d = paths[key]
        loaded = _cs_run_summary(d)
        if loaded is None:
            _warn(f"skipping {label}: no summary CSV in {d}")
            continue
        run, states = loaded
        run["label"] = label
        runs.append(run)
        for mode in ("diff", "poly"):
            bars.append(("%s · %s" % (label, mode), states[mode]))

    if runs:
        if len(runs) < 2:
            _warn("only %d of the 2 clean runs found; the slip-rate figure will "
                  "have fewer panels than the documented experiment" % len(runs))
        figs.append(figures.fig_slip_rate_bars(
            runs, os.path.join(png_dir, "vis_cs_slip_rate_bars.png"), lang=lang))
    if bars:
        figs.append(figures.fig_state_composition(
            bars, os.path.join(png_dir, "vis_cs_state_composition.png"), lang=lang))

    # --- one satellite's series, both detectors ---
    # The defaults are chosen to tell chapter 7's story rather than to be tidy:
    # G01 in the injected 1 Hz run is the arc that starts mid-file and then takes
    # the minimum detectable slip, and G24 in the 30 s sample is the satellite the
    # single-difference detector floods with flags. Defaulting to G01 in the
    # *clean* 1 Hz run would draw a satellite with no slips at all.
    for spec in (args.series or ["injected/run:G01", "sample-30s:G24"]):
        if ":" not in spec:
            _warn(f"ignoring --series '{spec}': expected DIR:SAT")
            continue
        where, sat = spec.rsplit(":", 1)
        d = where if os.path.isabs(where) else os.path.join(cs_dir, where)
        files = {"diff": os.path.join(d, "%s.gf.diff" % sat),
                 "poly": os.path.join(d, "%s.gf.poly" % sat)}
        if not os.path.isfile(files["diff"]):
            _warn(f"skipping series {sat}: {files['diff']} not found")
            continue
        views = {m: (cycleslip.series_view(files[m], m) if os.path.isfile(files[m])
                     else None) for m in ("diff", "poly")}
        thr = args.threshold
        if thr is None:
            meta = None
            summ = os.path.join(d, "summary.gf.diff.csv")
            if os.path.isfile(summ):
                from .io import load_gf_summary
                meta = load_gf_summary(summ)[2]
            thr = float(meta["threshold_m"]) if meta and "threshold_m" in meta else 0.030
        figs.append(figures.fig_slip_series(
            views["diff"], views["poly"],
            os.path.join(png_dir, "vis_cs_series_%s.png" % sat),
            thr=thr, sat=sat, run_label=where, lang=lang))

    # --- injected-slip scoreboard ---
    panels = []
    for key, ik, mk in (("run_1hz", "inject_1hz", "manifest_1hz"),
                        ("run_30s", "inject_30s", "manifest_30s")):
        label = "1 Hz zero baseline" if key == "run_1hz" else "30 s sample"
        panel = _cs_injection_panel(label, paths[mk], paths[ik], paths[ik + "_mw"])
        if panel is None:
            _warn(f"skipping the injection scoreboard for {label}: "
                  f"{paths[mk]} or {paths[ik]} not found")
            continue
        panels.append(panel)
    if panels:
        figs.append(figures.fig_injection_outcomes(
            panels, os.path.join(png_dir, "vis_cs_injection_outcomes.png"), lang=lang))

    # --- the MW combination: its own results, then the cross-check against GF ---
    mw_runs = []
    for key, label in (("run_1hz_mw", "1 Hz zero baseline"),
                       ("run_30s_mw", "30 s sample")):
        loaded = _cs_mw_run(paths[key], label)
        if loaded is None:
            _warn(f"skipping the MW slip-rate bars for {label}: "
                  f"no summary.mw.csv in {paths[key]}")
            continue
        mw_runs.append(loaded)
    if mw_runs:
        figs.append(figures.fig_slip_rate_bars(
            mw_runs, os.path.join(png_dir, "vis_cs_mw_slip_rate_bars.png"),
            lang=lang, detectors=(("mw", "legend_detector_mw"),),
            title_key="title_slip_rate_mw"))

    # --- one satellite's MW series ---
    # Defaults to the same satellite as the GF series figure, so the two can be
    # read against each other: on the 30 s sample G24 is the satellite the
    # single-difference detector floods, and the MW panel next to it is the
    # control that shows how little of that is a slip.
    for spec in (args.mw_series or ["sample-30s-mw:G24"]):
        if ":" not in spec:
            _warn(f"ignoring --mw-series '{spec}': expected DIR:SAT")
            continue
        where, sat = spec.rsplit(":", 1)
        path = os.path.join(cs_dir, where, "%s.mw" % sat)
        if not os.path.isfile(path):
            _warn(f"skipping the MW series {sat}: {path} not found")
            continue
        figs.append(figures.fig_mw_series(
            cycleslip.series_view(path, "mw"),
            os.path.join(png_dir, "vis_cs_mw_series_%s.png" % sat),
            sat=sat, run_label=where, lang=lang))

    # --- where the two combinations agree, per dataset ---
    xchecks = []
    for gf_key, mw_key, label in (("run_1hz", "run_1hz_mw", "1 Hz zero baseline"),
                                  ("run_30s", "run_30s_mw", "30 s sample")):
        gf_run = cycleslip.load_detector_run(paths[gf_key], "diff")
        mw_run = cycleslip.load_detector_run(paths[mw_key], "mw")
        if not (gf_run and mw_run):
            _warn(f"skipping the GF/MW cross-check for {label}: "
                  f"{paths[gf_key]} or {paths[mw_key]} is empty or missing")
            continue
        both, gf_only, mw_only = cycleslip.compare_flagged(gf_run, mw_run)
        # `compare_flagged` returns SETS, and Python randomises string hashes per
        # process, so iterating them directly draws the markers in a different
        # order every run - and overlapping markers composite differently, which
        # changes the PNG byte for byte. Sorting pins the figure down; without it
        # regenerating the docs figures can never reproduce them.
        xchecks.append({
            "label": label,
            "sats": sorted({k[0] for k in set(gf_run) | set(mw_run)}),
            "both": sorted((k[0], k[3]) for k in both),
            "gf_only": sorted((k[0], k[3]) for k in gf_only),
            "mw_only": sorted((k[0], k[3]) for k in mw_only),
            "counts": (len(gf_only) + len(both), len(mw_only) + len(both),
                       len(both)),
        })
    if xchecks:
        figs.append(figures.fig_crosscheck(
            xchecks, os.path.join(png_dir, "vis_cs_gf_mw_crosscheck.png"),
            lang=lang))

    # --- the two null spaces, on the injected 1 Hz run ---
    # One figure per direction. The chapter's claim is that the two blind spots
    # are complements, but a single figure can only draw one half of that: it
    # shows GF flat and MW stepping, and leaves "MW is blind to (k, k)" as a
    # table entry with nothing behind it.
    thr = args.threshold
    if thr is None:
        from .io import load_gf_summary
        summ = os.path.join(paths["inject_1hz"], "summary.gf.diff.csv")
        meta = load_gf_summary(summ)[2] if os.path.isfile(summ) else None
        thr = (float(meta["threshold_m"]) if meta and "threshold_m" in meta
               else 0.030)
    for case, png_name, title_key in _NULLSPACE_FIGURES:
        panel = _cs_nullspace_panel(paths, case)
        if panel is None:
            _warn(f"skipping the '{case}' null-space figure: the injected 1 Hz "
                  "runs or their manifest are missing (the 1 Hz zero baseline "
                  "is not committed)")
            continue
        gf_view, mw_view, target, k1, k2, epoch = panel
        figs.append(figures.fig_nullspace(
            gf_view, mw_view, os.path.join(png_dir, png_name),
            sat=target, dn1=k1, dn2=k2, epoch=epoch, thr=thr, lang=lang,
            title_key=title_key))

    if not figs:
        return _fail(f"no cycle-slip figures produced; nothing usable under {cs_dir}\n"
                     "  Run 'gnss app cs-detect-gf' first - see docs/cycle-slip-gf.md.")

    # Print what was plotted, so the figures can be checked against the CSVs
    # without opening either.
    for run in runs:
        d, p = run["diff"], run["poly"]
        print("%-20s diff %6d/%-7d = %5.2f%%   poly %5d/%-7d = %5.2f%%" % (
            run["label"], int(d["nSlip"]), int(d["nTested"]),
            100.0 * d["nSlip"] / d["nTested"] if d["nTested"] else 0.0,
            int(p["nSlip"]), int(p["nTested"]),
            100.0 * p["nSlip"] / p["nTested"] if p["nTested"] else 0.0))
    for run in mw_runs:
        m = run["mw"]
        print("%-20s mw   %6d/%-7d = %5.2f%%" % (
            run["label"], int(m["nSlip"]), int(m["nTested"]),
            100.0 * m["nSlip"] / m["nTested"] if m["nTested"] else 0.0))
    for x in xchecks:
        n_gf, n_mw, n_both = x["counts"]
        print("%-20s GF %6d   MW %6d   both %5d" % (x["label"], n_gf, n_mw, n_both))

    print()
    print(t(lang, "saved_to", dir=png_dir))
    for f in figs:
        print("  -", f)
    return 0


def cmd_app(args) -> int:
    root = _find.project_root()
    target = AUX_APPS.get(args.name)
    if not target:
        return _fail(f"unknown app '{args.name}'. Known: {', '.join(sorted(AUX_APPS))}")
    exe = _find.find_binary(target, root)
    if not exe:
        return _fail(f"{target} not found - run 'gnss build' first.")
    return subprocess.run([exe] + list(args.args or []), cwd=root).returncode


def cmd_ex(args) -> int:
    root = _find.project_root()
    if args.name not in TEACHING_EXAMPLES:
        return _fail(f"unknown example '{args.name}'. Known: {', '.join(TEACHING_EXAMPLES)}")
    exe = _find.find_binary(args.name, root)
    if not exe:
        return _fail(f"{args.name} not found - run 'gnss build' first.")
    return subprocess.run([exe] + list(args.args or []), cwd=root).returncode


# ---------------------------------------------------------------------------
# argument parsing
# ---------------------------------------------------------------------------

def _add_solver_args(p):
    p.add_argument("config", nargs="?", default=os.path.join(_find.project_root(), "config", "spp.ini"),
                   help="configuration file (default: config/spp.ini)")
    p.add_argument("--obs", help="RINEX observation file")
    p.add_argument("--nav", help="RINEX broadcast navigation file")
    p.add_argument("--out-dir", dest="out_dir", help="output directory")
    p.add_argument("--stop", help="stop epoch, e.g. 2025-01-01T00:30:30")
    p.add_argument("--mode", default="DUAL_IF", help="DUAL_IF (default) or DUAL_RAW")
    p.add_argument("--verbose", action="store_true", help="per-epoch progress")


def _add_plot_args(p, skip=()):
    """Add plotting options.

    `skip` names options already defined by a sibling group - `run` composes the
    solver and plot arguments, and `--out-dir`/`--mode` are meaningful to both,
    so they must only be registered once.
    """
    p.add_argument("--rinex", help="RINEX observation file (reference coordinates come from its header)")
    p.add_argument("--spp-out", dest="spp_out", help="position output file")
    p.add_argument("--pos-vel", dest="pos_vel", help="position+velocity output file")
    if "out_dir" not in skip:
        p.add_argument("--out-dir", dest="out_dir", help="directory holding the solver output")
    p.add_argument("--png-dir", dest="png_dir", help="directory for the figures")
    if "mode" not in skip:
        p.add_argument("--mode", default="DUAL_IF", help="solution mode, used in titles and file names")
    p.add_argument("--station", default="", help="station label for the report")
    p.add_argument("--lang", default="en", choices=list(LANGS),
                   help="figure and report language (default: en)")
    p.add_argument("--no-figures", action="store_true", help="report only, write no figures")
    p.add_argument("--no-report", dest="report", action="store_false", help="figures only")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="gnss",
        description="GnssLab - GNSS data processing: a C++ engine for SPP and "
                    "Doppler velocity, with a Python accuracy and plotting layer.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Try `gnss demo` first - it runs the bundled sample with no arguments.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    b = sub.add_parser("build", help="configure and build the C++ targets")
    b.add_argument("--build-dir", dest="build_dir", help="build directory (default: build)")
    b.add_argument("--build-type", dest="build_type", default="Release",
                   choices=["Release", "Debug", "RelWithDebInfo", "MinSizeRel"])
    b.add_argument("--generator", help="CMake generator (default: Ninja when available)")
    b.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    b.set_defaults(func=cmd_build)

    s = sub.add_parser("spp", help="run the SPP + velocity solver")
    _add_solver_args(s)
    s.set_defaults(func=cmd_spp)

    pl = sub.add_parser("plot", help="analyse and plot solver output")
    _add_plot_args(pl)
    pl.set_defaults(func=cmd_plot)

    r = sub.add_parser("run", help="spp + plot")
    _add_solver_args(r)
    _add_plot_args(r, skip=("out_dir", "mode"))
    r.set_defaults(func=cmd_run)

    d = sub.add_parser("demo", help="run end to end on the bundled sample")
    d.add_argument("--lang", default="en", choices=list(LANGS))
    d.add_argument("--station", default="")
    d.add_argument("--docs-figures", dest="docs_figures", action="store_true",
                   help="write figures into docs/figures/ (for the README)")
    d.set_defaults(func=cmd_demo)

    cs = sub.add_parser("cs-plot", help="plot cycle-slip detector output (chapter 7)")
    cs.add_argument("--cs-dir", dest="cs_dir",
                    help="root of the cycle-slip output tree (default: output/cs)")
    cs.add_argument("--run-1hz", dest="run_1hz",
                    help="1 Hz zero-baseline run (default: <cs-dir>/zero-1hz)")
    cs.add_argument("--run-30s", dest="run_30s",
                    help="30 s sample run (default: <cs-dir>/sample-30s)")
    cs.add_argument("--inject-1hz", dest="inject_1hz",
                    help="1 Hz injected run (default: <cs-dir>/injected/run)")
    cs.add_argument("--inject-30s", dest="inject_30s",
                    help="30 s injected run (default: <cs-dir>/injected/run-30s)")
    cs.add_argument("--manifest-1hz", dest="manifest_1hz",
                    help="1 Hz ground truth (default: <cs-dir>/injected/oem719-injected.obs.slips.csv)")
    cs.add_argument("--manifest-30s", dest="manifest_30s",
                    help="30 s ground truth (default: <cs-dir>/injected/wuh2-injected.rnx.slips.csv)")
    cs.add_argument("--run-1hz-mw", dest="run_1hz_mw",
                    help="1 Hz MW run (default: <cs-dir>/zero-1hz-mw)")
    cs.add_argument("--run-30s-mw", dest="run_30s_mw",
                    help="30 s MW run (default: <cs-dir>/sample-30s-mw)")
    cs.add_argument("--inject-1hz-mw", dest="inject_1hz_mw",
                    help="1 Hz injected MW run (default: <cs-dir>/injected/run-mw)")
    cs.add_argument("--inject-30s-mw", dest="inject_30s_mw",
                    help="30 s injected MW run (default: <cs-dir>/injected/run-30s-mw)")
    cs.add_argument("--series", action="append", default=None, metavar="DIR:SAT",
                    help="draw one satellite's GF series (repeatable; default: the two "
                         "documented ones, injected/run:G01 and sample-30s:G24)")
    cs.add_argument("--mw-series", dest="mw_series", action="append", default=None,
                    metavar="DIR:SAT",
                    help="draw one satellite's MW series (repeatable; default: "
                         "sample-30s-mw:G24, the same satellite as the GF one)")
    cs.add_argument("--threshold", type=float, default=None,
                    help="detection threshold in metres (default: the run's own metadata)")
    cs.add_argument("--png-dir", dest="png_dir", help="directory for the figures")
    cs.add_argument("--docs-figures", dest="docs_figures", action="store_true",
                    help="write figures into docs/figures/ (for the analysis document)")
    cs.add_argument("--lang", default="en", choices=list(LANGS),
                    help="figure language (default: en)")
    cs.set_defaults(func=cmd_cs_plot)

    a = sub.add_parser("app", help="run an auxiliary program")
    a.add_argument("name", help=" | ".join(sorted(AUX_APPS)))
    a.add_argument("args", nargs=argparse.REMAINDER)
    a.set_defaults(func=cmd_app)

    e = sub.add_parser("ex", help="run a teaching example")
    e.add_argument("name", help=" | ".join(TEACHING_EXAMPLES))
    e.add_argument("args", nargs=argparse.REMAINDER)
    e.set_defaults(func=cmd_ex)

    return p


def main(argv=None) -> int:
    _force_utf8_stdout()
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
