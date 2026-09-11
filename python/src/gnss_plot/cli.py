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
