# -*- coding: utf-8 -*-
"""RTK float accuracy: reading a run, rotating it into ENU, and scoring it.

Chapter 8's program, ``apps/rtk_float.cpp``, writes three files per
constellation into one output directory::

    <rover>_<sys>_rtk_float.out    one line per epoch: the SPP and the RTK XYZ
    <rover>_<sys>_rtk_diag.csv     per-epoch conditioning and post-fit columns
    <rover>_<sys>_manifest.json    what produced the above

Reading those and deciding what the numbers mean is analysis rather than
parsing, which is why it lives here and not in :mod:`gnss_plot.io`. Two
consumers share the module and nothing else: the ``gnss rtk-plot`` command and
any script that wants the same table.

Two things about the numbers are load-bearing, and the report says both out
loud because a reader who takes the bias for an accuracy figure will be wrong
by two orders of magnitude:

* **The bias is a datum offset, not an accuracy figure.**
  ``SPPUCCodePhase::solve`` builds the base station's equations at its RINEX
  header coordinate and returns without iterating (``if (!isRover) break;``),
  and the rover is accepted as soon as ``|dxyz| < 0.1`` m. Every epoch
  therefore reproduces *the base header position plus the rover's last,
  unconverged correction* - a constant offset of up to 0.1 m that is common to
  the whole run and cancels only in a difference between two solutions of the
  same run. The per-axis bias and the per-axis sigma are reported as separate
  rows for exactly this reason.
* **The SPP/RTK ratio is the one reference-independent number.**
  Both columns are differenced against the same base header, so whatever error
  the header itself carries (it is only metre-to-decametre accurate) is the
  same in both and cancels in the ratio - even though it does not cancel in
  either RMS.
"""

from __future__ import annotations

import csv
import os
import unicodedata

import numpy as np

from . import coords, io
from ._strings import STRINGS, t
from .stats import component_stats, enu_stats

__all__ = [
    "MODES",
    "MODE_LABELS",
    "BASE_HEADER_XYZ",
    "OUT_SUFFIX",
    "FIX_SUFFIX",
    "DIAG_SUFFIX",
    "MANIFEST_SUFFIX",
    "PERCENTILES",
    "DEFAULT_REF_SOURCE",
    "DEFAULT_RATIO_THRESHOLD",
    "load_rtk_float",
    "load_rtk_fixed",
    "load_rtk_diag",
    "fix_summary",
    "fixed_series",
    "find_runs",
    "select_runs",
    "runs_from_outputs",
    "resolve_reference",
    "load_run",
    "enu_errors",
    "error_stats",
    "mode_label",
    "comparison_table",
    "report",
]

#: The constellations ``apps/rtk_float.cpp`` has a mode for, in the order the
#: tables and figures draw them. Anything else found on disk is still read, but
#: sorts after these and falls back to its own name as a label.
MODES = ("gps", "bds2", "bds3")

#: Short names for the figures. The full ``sysLabel`` from the manifest
#: ("GPS L1/L2 (C1C+C2W)") is what the table's system column carries.
MODE_LABELS = {"gps": "GPS", "bds2": "BDS-2", "bds3": "BDS-3"}

#: The reference every error in this module is measured against: the base
#: station's ``APPROX POSITION XYZ``, from
#: ``data/Zero-baseline/oem719-202203031500-2.obs``.
#:
#: It is a *default*, not a constant, and deliberately so: ``data/`` is
#: gitignored, so nothing here may read it at run time. Callers that have the
#: RINEX file pass it to :func:`resolve_reference`, which parses the header;
#: this literal is the fallback so that plotting still works from a checkout
#: that has the solver output but not the input.
BASE_HEADER_XYZ = (-2267812.4743, 5009352.1093, 3221012.1444)

#: What :func:`resolve_reference` puts in `source` when it had to fall back.
#: A sentinel string rather than ``None`` because the report prints it verbatim.
DEFAULT_REF_SOURCE = "built-in default (base RINEX not supplied)"

OUT_SUFFIX = "_rtk_float.out"
FIX_SUFFIX = "_rtk_fixed.out"
DIAG_SUFFIX = "_rtk_diag.csv"
MANIFEST_SUFFIX = "_manifest.json"

#: Percentiles of the 3-D error magnitude that the chapter quotes. ``max`` is
#: carried alongside them rather than as a fourth percentile.
PERCENTILES = (50.0, 68.0, 95.0)

#: The diagnostic CSV's numeric columns, in file order after ``sod``.
#: ``datumSat`` is the one text column and is read separately.
DIAG_NUMERIC = ("sod", "nRoverEq", "nSD", "nDD", "nUnk", "rank", "cond",
                "datumFallback", "nSDsats", "absDxyz", "sigma0", "postfitRms",
                # The fixing columns, present whether or not fixing ran.
                # ``ratio`` is 0 when no integer candidate existed, which is
                # also what it is on a run that did not attempt a fix at all.
                "nAmb", "ratio", "fixed", "absDxyzFixed",
                # The estimated BDS-2/BDS-3 receiver inter-system bias, metres.
                # 0 when the epoch did not estimate one - which is every epoch
                # of a run without --isb, and every epoch of a mixed run whose
                # satellites all came from one generation.
                "isb")


# ---------------------------------------------------------------------------
# reading
# ---------------------------------------------------------------------------

def load_rtk_float(fn: str):
    """Read ``<rover>_<sys>_rtk_float.out``.

    Returns ``(sod, spp_xyz, rtk_xyz)``: seconds of day as ``(n,)``, and the two
    position series as ``(n, 3)`` ECEF metre arrays.

    The file has no header, so the columns are positional, exactly as
    ``printSolution`` writes them::

        year doy sod timeSystem "spp:" X Y Z "rtk:" X Y Z

    ``src/GnssFunc.cpp`` has a second overload of ``printSolution`` that writes
    ``float-rtk: ... ratio: ... fixed-rtk: ...`` instead, so the two label
    tokens are **checked rather than assumed** - a fixed-ambiguity file read
    positionally would otherwise produce a full table of plausible, wrong
    numbers. Rows that do not carry both labels are skipped, which is how the
    rest of this package treats an unparseable line; the caller sees an empty
    result rather than an exception several frames down.
    """
    sod, spp, rtk = [], [], []
    with open(fn, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            p = ln.split()
            if len(p) < 12 or p[4] != "spp:" or p[8] != "rtk:":
                continue
            try:
                sod.append(float(p[2]))
                spp.append([float(p[5]), float(p[6]), float(p[7])])
                rtk.append([float(p[9]), float(p[10]), float(p[11])])
            except ValueError:
                continue
    return (np.asarray(sod, dtype=float),
            np.asarray(spp, dtype=float).reshape(-1, 3),
            np.asarray(rtk, dtype=float).reshape(-1, 3))


def load_rtk_fixed(fn: str):
    """Read ``<rover>_<sys>_rtk_fixed.out``, written by ``--fix``.

    Returns ``(sod, spp_xyz, float_xyz, ratio, fixed_xyz)``.

    The layout is the textbook main program's, and it is **not** the float
    file's layout with more columns::

        year doy sod timeSystem "spp:" X Y Z "float-rtk:" X Y Z "ratio:"R "fixed-rtk:"X Y Z

    ``ratio:`` and ``fixed-rtk:`` are glued to their values with no space,
    exactly as the notes' own program prints them, so every field is located by
    its label rather than by column position. Reading this file with
    :func:`load_rtk_float` would produce a full table of plausible, wrong
    numbers - hence the separate loader.

    The float column is repeated here rather than being looked up in the sibling
    float file, because the two are then guaranteed to come from one run; the
    regression test asserts they agree anyway, as a check on the writer.
    """
    sod, spp, flt, ratio, fix = [], [], [], [], []
    with open(fn, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            p = ln.split()
            if not any(tok.startswith("fixed-rtk:") for tok in p):
                continue
            try:
                i = p.index("spp:")
                j = p.index("float-rtk:")
                k = next(n for n, tok in enumerate(p) if tok.startswith("ratio:"))
                m = next(n for n, tok in enumerate(p) if tok.startswith("fixed-rtk:"))
                sod.append(float(p[2]))
                spp.append([float(p[i + 1]), float(p[i + 2]), float(p[i + 3])])
                flt.append([float(p[j + 1]), float(p[j + 2]), float(p[j + 3])])
                ratio.append(float(p[k][len("ratio:"):]))
                fix.append([float(p[m][len("fixed-rtk:"):]),
                            float(p[m + 1]), float(p[m + 2])])
            except (ValueError, IndexError, StopIteration):
                continue
    return (np.asarray(sod, dtype=float),
            np.asarray(spp, dtype=float).reshape(-1, 3),
            np.asarray(flt, dtype=float).reshape(-1, 3),
            np.asarray(ratio, dtype=float),
            np.asarray(fix, dtype=float).reshape(-1, 3))


def load_rtk_diag(fn: str):
    """Read ``<rover>_<sys>_rtk_diag.csv``.

    Returns ``(sod, cols, datum)``: seconds of day, a ``{column: array}`` map
    over :data:`DIAG_NUMERIC` minus ``sod``, and the ``datumSat`` string array
    (``G08`` and friends).

    Columns are located by the file's own header rather than by position, so a
    column added to the C++ writer later does not shift the ones already read.
    A row whose ``sod`` or whose requested columns do not parse is skipped.
    """
    with open(fn, "r", encoding="utf-8", errors="replace", newline="") as f:
        reader = csv.DictReader(f)
        header = [h.strip() for h in (reader.fieldnames or [])]
        if "sod" not in header:
            return (np.asarray([], dtype=float), {}, np.asarray([], dtype=str))

        usable = [c for c in DIAG_NUMERIC if c in header]
        sod, datum = [], []
        cols: dict[str, list[float]] = {c: [] for c in usable if c != "sod"}
        for row in reader:
            try:
                values = {c: float(row[c]) for c in usable}
            except (TypeError, ValueError):
                continue
            sod.append(values["sod"])
            for c in cols:
                cols[c].append(values[c])
            datum.append(str(row.get("datumSat", "")).strip())

    return (np.asarray(sod, dtype=float),
            {k: np.asarray(v, dtype=float) for k, v in cols.items()},
            np.asarray(datum, dtype=str))


# ---------------------------------------------------------------------------
# locating a run
# ---------------------------------------------------------------------------

def _run_paths(out_path: str) -> dict:
    """The three sibling files of one ``*_rtk_float.out``, and its identity.

    ``rover`` and ``mode`` come out of the filename, which is the only place
    the C++ records them in a form that survives being copied elsewhere: the
    file is ``<rover>_<mode>_rtk_float.out`` and the rover name may itself
    contain underscores, so the split is taken from the right.
    """
    name = os.path.basename(out_path)
    stem = name[:-len(OUT_SUFFIX)] if name.endswith(OUT_SUFFIX) else name
    rover, _, mode = stem.rpartition("_")
    sibling = out_path[:-len(OUT_SUFFIX)] if out_path.endswith(OUT_SUFFIX) else out_path
    return {
        "rover": rover or stem,
        "mode": mode,
        "out": out_path,
        "fixed": sibling + FIX_SUFFIX,
        "diag": sibling + DIAG_SUFFIX,
        "manifest": sibling + MANIFEST_SUFFIX,
    }


def find_runs(directory: str, max_depth: int = 3, modes=None) -> list[dict]:
    """Every RTK run under `directory`, as a deterministic, sorted list.

    Recursive, because the full runs live one level below the C++ default:
    ``output/rtk`` holds a 25-epoch smoke run and ``output/rtk/full`` the
    7934-epoch ones the chapter quotes. Search order never leaks into the
    result - ``os.walk`` has its directory lists sorted in place and the output
    is sorted again by ``(rover, mode, path)`` - so two machines agree.
    """
    wanted = tuple(modes) if modes else MODES
    base = os.path.abspath(directory)
    depth0 = base.rstrip("\\/").count(os.sep)
    found: list[dict] = []

    for dirpath, dirnames, filenames in os.walk(base):
        dirnames.sort()
        if dirpath.rstrip("\\/").count(os.sep) - depth0 >= max_depth:
            dirnames[:] = []
        for name in sorted(filenames):
            if not name.endswith(OUT_SUFFIX):
                continue
            run = _run_paths(os.path.join(dirpath, name))
            if run["mode"] not in wanted:
                continue
            found.append(run)

    def rank(run):
        mode = run["mode"]
        return (run["rover"], MODES.index(mode) if mode in MODES else len(MODES),
                mode, run["out"])

    return sorted(found, key=rank)


def _count_rows(path: str) -> int:
    """Epoch lines in a solution file - used only to choose between runs.

    A plain count of non-empty lines, not a parse: it runs over every candidate
    before one of them is read properly, and the files all come from the same
    writer.
    """
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return sum(1 for ln in f if ln.strip())
    except OSError:
        return 0


def select_runs(runs, rover: str | None = None, modes=None) -> list[dict]:
    """One run per constellation, from what :func:`find_runs` returned.

    When the same constellation appears twice under the tree the **longer run
    wins**: the chapter's numbers come from the 7934-epoch runs, and a 25-second
    smoke test sitting beside them in the same output directory should not
    silently replace them. Equal lengths break on the path, so the choice never
    depends on filesystem order. The result is ordered like :data:`MODES`.
    """
    picked = list(runs)
    if rover:
        picked = [r for r in picked if r["rover"] == rover]
    if modes:
        wanted = tuple(modes)
        picked = [r for r in picked if r["mode"] in wanted]

    best: dict[str, tuple[int, dict]] = {}
    for run in picked:
        n = _count_rows(run["out"])
        current = best.get(run["mode"])
        if current is None or n > current[0] or (n == current[0]
                                                 and run["out"] < current[1]["out"]):
            best[run["mode"]] = (n, run)

    ordered = [m for m in MODES if m in best]
    ordered += sorted(m for m in best if m not in MODES)
    return [best[m][1] for m in ordered]


def runs_from_outputs(paths) -> list[dict]:
    """Build run records from explicit ``*_rtk_float.out`` paths.

    The escape hatch for a run whose files were copied somewhere flat: the
    ``.out`` is named, and the diagnostic and manifest are its siblings.
    """
    return [_run_paths(os.path.abspath(p)) for p in paths]


def resolve_reference(base_obs: str | None = None, root: str | None = None,
                      lang: str = "en"):
    """The ECEF reference the errors are measured against.

    Returns ``(ref_xyz, source)``. With a readable `base_obs`, the position
    comes from that RINEX file's ``APPROX POSITION XYZ`` - the same header the
    C++ builds the base station's equations on, so no constant has to be kept
    in step with the data. Otherwise it falls back to
    :data:`BASE_HEADER_XYZ`, and `source` says which of the two was used so the
    report can print it.
    """
    if base_obs:
        path = os.path.normpath(
            base_obs if os.path.isabs(base_obs) else os.path.join(root or "", base_obs))
        if os.path.isfile(path):
            return np.asarray(io.read_approx_position(path, lang=lang), dtype=float), path
    return np.asarray(BASE_HEADER_XYZ, dtype=float), DEFAULT_REF_SOURCE


# ---------------------------------------------------------------------------
# statistics
# ---------------------------------------------------------------------------

def enu_errors(xyz, ref_xyz):
    """ECEF positions -> local ENU error against `ref_xyz`.

    A one-line adapter over :func:`gnss_plot.coords.enu_position_error`, which
    is where the rotation itself lives; it exists so the ``(n, 3)`` array shape
    this module reads is unpacked in one place.
    """
    xyz = np.asarray(xyz, dtype=float)
    if xyz.size == 0:
        empty = np.asarray([], dtype=float)
        return empty, empty, empty
    return coords.enu_position_error(xyz[:, 0], xyz[:, 1], xyz[:, 2], ref_xyz)


def error_stats(sod, spp_xyz, rtk_xyz, ref_xyz):
    """Everything the table and the figures need, from two XYZ series.

    Returns a dict:

    ``n``
        epochs read.
    ``spp``, ``rtk``
        each the dict :func:`gnss_plot.stats.enu_stats` returns - ``bias``,
        ``std`` (the internal sigma, ``ddof=1``), ``rms`` per axis, plus
        ``rms_h`` and ``rms_3d``. Note the two conventions answer different
        questions: ``rms`` is about the reference and includes the offset,
        ``std`` is about the series' own mean and does not.
    ``ecef``
        the same per-axis sigma taken on the raw X/Y/Z differences instead of
        in ENU. Reported because the natural first reading of the solver's
        output is axis-by-axis in ECEF, and because the trace of a covariance
        is rotation-invariant: the three ECEF sigmas and the three ENU sigmas
        square-sum to the same number, so only the split between axes differs.
    ``percentile``
        ``p50``/``p68``/``p95``/``max`` of the 3-D error magnitude |d|. |d| is
        invariant under the ENU rotation, so these are the same numbers either
        way.
    ``ratio``
        SPP 3-D RMS over RTK 3-D RMS. This is the chapter's headline: it is the
        one figure here that does not depend on the reference, because both
        columns are differenced against the same base header.
    ``rtk_enu``, ``spp_enu``
        the ``(E, N, U)`` arrays themselves, for the time-series figure.
    """
    sod = np.asarray(sod, dtype=float)
    spp_e, spp_n, spp_u = enu_errors(spp_xyz, ref_xyz)
    rtk_e, rtk_n, rtk_u = enu_errors(rtk_xyz, ref_xyz)

    spp = enu_stats(spp_e, spp_n, spp_u)
    rtk = enu_stats(rtk_e, rtk_n, rtk_u)

    d3 = np.sqrt(rtk_e ** 2 + rtk_n ** 2 + rtk_u ** 2)
    if d3.size:
        pct = [float(v) for v in np.percentile(d3, list(PERCENTILES))]
        pct.append(float(d3.max()))
    else:
        pct = [float("nan")] * (len(PERCENTILES) + 1)

    spp_xyz = np.asarray(spp_xyz, dtype=float) - np.asarray(ref_xyz, dtype=float)
    rtk_xyz = np.asarray(rtk_xyz, dtype=float) - np.asarray(ref_xyz, dtype=float)
    ecef_rtk = [component_stats(rtk_xyz[:, k]) if rtk_xyz.size
                else (float("nan"),) * 3 for k in range(3)]
    ecef_spp = [component_stats(spp_xyz[:, k]) if spp_xyz.size
                else (float("nan"),) * 3 for k in range(3)]

    ratio = (spp["rms_3d"] / rtk["rms_3d"]) if rtk["rms_3d"] else float("inf")

    return {
        "n": int(sod.size),
        "spp": spp,
        "rtk": rtk,
        "ecef": {
            "rtk": {"bias": np.array([r[0] for r in ecef_rtk]),
                    "sigma": np.array([r[1] for r in ecef_rtk]),
                    "rms": np.array([r[2] for r in ecef_rtk])},
            "spp": {"bias": np.array([r[0] for r in ecef_spp]),
                    "sigma": np.array([r[1] for r in ecef_spp]),
                    "rms": np.array([r[2] for r in ecef_spp])},
        },
        "percentile": dict(zip(("p50", "p68", "p95", "max"), pct)),
        "ratio": float(ratio),
        "rtk_enu": (rtk_e, rtk_n, rtk_u),
        "spp_enu": (spp_e, spp_n, spp_u),
    }


#: How far above the run's own median sigma0 an epoch has to sit to be called a
#: gross-error epoch. Ten is the factor docs/rtk.md §6 uses, and it is a ratio
#: rather than an absolute threshold because sigma0's scale moves with the
#: constellation - BDS-2's median is four times GPS's and nine times BDS-3's,
#: which is the same story the accuracy table tells.
_GROSS_ERROR_FACTOR = 10.0


def diag_summary(diag) -> dict | None:
    """The few conditioning numbers worth putting beside the accuracies.

    Returns None when the run has no diagnostic CSV, and a dict of counts and
    the median otherwise. ``outliers`` is the count of epochs whose ``sigma0``
    exceeds :data:`_GROSS_ERROR_FACTOR` times the run's own median - the
    post-fit residual is the one diagnostic the solver did not have before
    chapter 8, and the count is what says whether a run's worst epochs are a
    handful of bad observations or the whole solution coming apart.
    """
    if diag is None:
        return None
    sod, cols, datum = diag
    if "sigma0" not in cols or cols["sigma0"].size == 0:
        return None
    sigma0 = cols["sigma0"]
    median = float(np.median(sigma0))
    out = {
        "n": int(sod.size),
        "sigma0_median": median,
        "outliers": int((sigma0 > _GROSS_ERROR_FACTOR * median).sum())
        if median > 0 else 0,
        "datum_fallback": int(cols["datumFallback"].sum())
        if "datumFallback" in cols else 0,
    }
    if "rank" in cols and "nUnk" in cols:
        out["full_rank"] = bool(np.all(cols["rank"] == cols["nUnk"]))
    return out


#: The ratio-test threshold used when a run has no manifest to read its own
#: from. Matches ``RTKConfigData::defaults()``.
DEFAULT_RATIO_THRESHOLD = 3.0


def fix_summary(sod, spp_xyz, fixed_xyz, ratio, ref_xyz, threshold):
    """Score a ``--fix`` run: how much of it was fixed, and how good that is.

    Returns None if `ratio` is empty (a run without ``--fix``). Otherwise:

    ``threshold``
        the ratio threshold applied, as a number.
    ``n``, ``n_accepted``, ``fraction``
        epochs, how many cleared the threshold, and that as a fraction.
    ``stats_all``
        :func:`error_stats` over the fixed file **exactly as written** - which
        is the float solution on every epoch the ratio test rejected. This is
        what a consumer that ignores the ratio ends up with, and on BDS-2 it is
        *worse* than the float solution, which is the whole argument for
        applying the test.
    ``stats_accepted``
        the same over the accepted epochs only. This is the number the method
        should be judged by.
    ``worst_accepted_m``
        the largest 3-D error among accepted epochs - the one figure that says
        whether any fix was wrong.
    ``ratio_p50``, ``ratio_max``
        the ratio distribution's middle and top, so a reader can see how much
        margin the accepted epochs had.
    """
    ratio = np.asarray(ratio, dtype=float)
    if ratio.size == 0:
        return None

    accepted = ratio > threshold
    n = int(ratio.size)
    n_acc = int(accepted.sum())

    def score(mask):
        if not np.any(mask):
            return None
        return error_stats(np.asarray(sod)[mask], np.asarray(spp_xyz)[mask],
                           np.asarray(fixed_xyz)[mask], ref_xyz)

    stats_all = score(np.ones(n, dtype=bool))
    stats_acc = score(accepted)

    worst = float("nan")
    if stats_acc is not None:
        e, nu, u = (np.asarray(a, dtype=float) for a in stats_acc["rtk_enu"])
        d = np.sqrt(e ** 2 + nu ** 2 + u ** 2)
        worst = float(d.max()) if d.size else float("nan")

    return {
        "threshold": float(threshold),
        "n": n,
        "n_accepted": n_acc,
        "fraction": (n_acc / n) if n else 0.0,
        "accepted": accepted,
        "ratios": ratio,
        "stats_all": stats_all,
        "stats_accepted": stats_acc,
        "worst_accepted_m": worst,
        "ratio_p50": float(np.median(ratio)),
        "ratio_max": float(ratio.max()),
    }


def fixed_series(result, threshold=None):
    """Inputs for :func:`gnss_plot.figures.fig_rtk_float_vs_fixed_ts`.

    ``(label, sod, mag_float, mag_fixed, accepted)`` for one run, or None when
    the run was not fixed. `mag_float` and `mag_fixed` are 3-D error magnitudes
    in metres; `accepted` is the boolean mask the figure draws only the fixed
    series inside.

    The float magnitude comes from the same arrays the float figures use, so the
    grey series here and the series in figures 8-1/8-2 cannot drift apart.
    """
    fix = result.get("fix")
    if not fix:
        return None

    def mag(enu):
        e, nu, u = (np.asarray(a, dtype=float) for a in enu)
        return np.sqrt(e ** 2 + nu ** 2 + u ** 2)

    if threshold is None:
        threshold = fix["threshold"]
    accepted = np.asarray(fix["accepted"], dtype=bool) if "accepted" in fix \
        else np.asarray([], dtype=bool)

    stats_all = fix["stats_all"]
    if stats_all is None:
        return None
    return (result["label"], np.asarray(result["sod"], dtype=float),
            mag(result["stats"]["rtk_enu"]), mag(stats_all["rtk_enu"]), accepted)


def load_run(run: dict, ref_xyz) -> dict:
    """Read one run's files and score it. `run` comes from :func:`find_runs`."""
    sod, spp_xyz, rtk_xyz = load_rtk_float(run["out"])
    diag = load_rtk_diag(run["diag"]) if os.path.isfile(run["diag"]) else None
    manifest = io.read_manifest(run["manifest"]) if os.path.isfile(run["manifest"]) else None

    # The fixed solution is optional: it exists only when the solver ran with
    # --fix, and that is recorded in the manifest rather than inferable from the
    # float file.
    fix = None
    fixed_path = run.get("fixed")
    if fixed_path and os.path.isfile(fixed_path):
        threshold = DEFAULT_RATIO_THRESHOLD
        if manifest and manifest.get("ratioThreshold") is not None:
            threshold = float(manifest["ratioThreshold"])
        fsod, fspp, _fflt, fratio, ffixed = load_rtk_fixed(fixed_path)
        if fsod.size:
            fix = fix_summary(fsod, fspp, ffixed, fratio, ref_xyz, threshold)

    return {
        "mode": run["mode"],
        "rover": run["rover"],
        "label": mode_label(run["mode"]),
        "sod": sod,
        "diag": diag,
        "diag_summary": diag_summary(diag),
        "manifest": manifest,
        "stats": error_stats(sod, spp_xyz, rtk_xyz, ref_xyz),
        "fix": fix,
        "has_fix": fix is not None,
    }


def mode_label(mode: str, lang: str = "en") -> str:
    """A constellation's short name for a legend or a table row."""
    table = STRINGS.get(lang) or STRINGS["en"]
    return table.get("legend_sys_" + mode) or MODE_LABELS.get(mode, mode)


# ---------------------------------------------------------------------------
# the table
# ---------------------------------------------------------------------------

def _width(s: str) -> int:
    """Display width, counting East Asian wide characters as two columns.

    Without this the Chinese table is padded to the wrong width and every
    column to its right slides, because ``str.ljust`` counts code points and a
    terminal does not.
    """
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)


def _pad(s: str, width: int, align: str = "<") -> str:
    gap = max(width - _width(s), 0)
    return (s + " " * gap) if align == "<" else (" " * gap + s)


def _row(cells, widths, aligns) -> str:
    return "  " + "  ".join(_pad(c, w, a) for c, w, a in zip(cells, widths, aligns))


def comparison_table(results, lang: str = "en") -> str:
    """The whole comparison as plain text - three tables, no prose.

    * the headline: epochs, SPP and RTK 3-D RMS, and the SPP/RTK ratio;
    * the 3-D error magnitude's 50th, 68th, 95th and largest percentiles;
    * the per-axis numbers, with **bias and sigma on separate rows** and the
      frame named, because the two are not the same kind of quantity here (see
      the module docstring).

    Returns a string rather than printing, so a caller can quote it in a
    document or a test.
    """
    lines: list[str] = []

    # --- headline -----------------------------------------------------------
    cols = [t(lang, "rpt_rtk_c_sys"), t(lang, "rpt_rtk_c_system"),
            t(lang, "rpt_rtk_c_epochs"), t(lang, "rpt_rtk_c_spp"),
            t(lang, "rpt_rtk_c_rtk"), t(lang, "rpt_rtk_c_ratio")]
    widths = [13, 27, 8, 13, 13, 9]
    aligns = "<" * 2 + ">" * 4
    lines.append("  " + t(lang, "rpt_rtk_head", ratio=_ratio_headline(results, lang)))
    lines.append("")
    lines.append(_row(cols, widths, aligns))
    lines.append("  " + "  ".join("-" * w for w in widths))
    for r in results:
        s = r["stats"]
        sys_label = (r["manifest"] or {}).get("sysLabel") or r["label"]
        lines.append(_row([r["label"], sys_label, "%d" % s["n"],
                           "%.3f m" % s["spp"]["rms_3d"],
                           "%.3f m" % s["rtk"]["rms_3d"],
                           "%.1fx" % s["ratio"] if np.isfinite(s["ratio"]) else "-"],
                          widths, aligns))

    # --- 3-D error percentiles ---------------------------------------------
    cols = [t(lang, "rpt_rtk_c_sys"), t(lang, "rpt_rtk_c_p50"),
            t(lang, "rpt_rtk_c_p68"), t(lang, "rpt_rtk_c_p95"),
            t(lang, "rpt_rtk_c_max"), t(lang, "rpt_rtk_c_outliers")]
    widths = [13, 12, 12, 12, 12, 13]
    aligns = "<" + ">" * 5
    lines.append("")
    lines.append("  " + t(lang, "rpt_rtk_pct_head"))
    lines.append(_row(cols, widths, aligns))
    lines.append("  " + "  ".join("-" * w for w in widths))
    for r in results:
        p = r["stats"]["percentile"]
        summary = r.get("diag_summary")
        lines.append(_row([r["label"]] + ["%.3f m" % p[k]
                                          for k in ("p50", "p68", "p95", "max")]
                          + ["%d" % summary["outliers"] if summary else "-"],
                          widths, aligns))

    # --- per-axis bias and sigma, one row per quantity ----------------------
    # Bias and sigma are separate ROWS, not two columns of one number, because
    # they answer different questions here: the sigma is the run's precision,
    # the bias is where the base station's header coordinate put the datum.
    # Sigma is printed unsigned and bias signed, so the two cannot be read as
    # each other at a glance.
    rows = (
        ("ENU", "rpt_rtk_q_spp_bias", lambda s: s["spp"]["bias"], "%+.4f"),
        ("ENU", "rpt_rtk_q_spp_sigma", lambda s: s["spp"]["std"], "%.4f"),
        ("ENU", "rpt_rtk_q_bias", lambda s: s["rtk"]["bias"], "%+.4f"),
        ("ENU", "rpt_rtk_q_sigma", lambda s: s["rtk"]["std"], "%.4f"),
        # The same two RTK quantities on the raw ECEF axes. The trace of a
        # covariance is rotation-invariant, so the three of either frame
        # square-sum to the same total and only the split between axes differs;
        # ECEF is included because that is the frame the solver's own columns
        # are in, and a per-axis reading of the `.out` file lands here.
        ("ECEF", "rpt_rtk_q_bias", lambda s: s["ecef"]["rtk"]["bias"], "%+.4f"),
        ("ECEF", "rpt_rtk_q_sigma", lambda s: s["ecef"]["rtk"]["sigma"], "%.4f"),
        ("ENU", "rpt_rtk_q_rms", lambda s: s["rtk"]["rms"], "%.4f"),
    )
    cols = [t(lang, "rpt_rtk_c_sys"), t(lang, "rpt_rtk_c_frame"),
            t(lang, "rpt_rtk_c_quantity"), "E / X", "N / Y", "U / Z"]
    widths = [13, 6, 15, 11, 11, 11]
    aligns = "<" + ">" * 5
    lines.append("")
    lines.append("  " + t(lang, "rpt_rtk_axis_head"))
    lines.append(_row(cols, widths, aligns))
    lines.append("  " + "  ".join("-" * w for w in widths))
    for r in results:
        s = r["stats"]
        for frame, key, get, fmt in rows:
            tri = np.asarray(get(s), dtype=float)
            lines.append(_row([r["label"], frame, t(lang, key)]
                              + [fmt % v for v in tri], widths, aligns))
        lines.append(_row([r["label"], "ENU", t(lang, "rpt_rtk_q_rms3d"),
                           "", "", "%.4f" % s["rtk"]["rms_3d"]], widths, aligns))

    # --- the fixed solution, when the run has one ---------------------------
    # The one table here whose columns are a *comparison* rather than a
    # description: the float and fixed columns are the same epochs scored twice,
    # so the difference between them is the whole point. Both are 3-D RMS in
    # ENU, and `worst` is the largest error among the epochs the ratio test
    # accepted - the number that says whether any fix was wrong.
    fixed_results = [r for r in results if r.get("fix")]
    if fixed_results:
        cols = [t(lang, "rpt_rtk_c_sys"), t(lang, "rpt_rtk_c_epochs"),
                t(lang, "rpt_rtk_c_fixedrate"), t(lang, "rpt_rtk_c_float"),
                t(lang, "rpt_rtk_c_fixed"), t(lang, "rpt_rtk_c_worstfix"),
                t(lang, "rpt_rtk_c_gain"), t(lang, "rpt_rtk_c_ratiop50")]
        widths = [13, 8, 11, 13, 13, 13, 9, 11]
        aligns = "<" + ">" * 7
        lines.append("")
        lines.append("  " + t(lang, "rpt_rtk_fix_head",
                               thr="%.1f" % fixed_results[0]["fix"]["threshold"]))
        lines.append(_row(cols, widths, aligns))
        lines.append("  " + "  ".join("-" * w for w in widths))
        for r in fixed_results:
            f = r["fix"]
            s = r["stats"]
            acc = f["stats_accepted"]
            acc_rms = acc["rtk"]["rms_3d"] if acc else float("nan")
            gain = (s["rtk"]["rms_3d"] / acc_rms) if acc_rms else float("inf")
            lines.append(_row([r["label"], "%d" % f["n"],
                               "%.1f%%" % (100.0 * f["fraction"]),
                               "%.3f m" % s["rtk"]["rms_3d"],
                               "%.4f m" % acc_rms,
                               "%.4f m" % f["worst_accepted_m"],
                               ("%.0fx" % gain) if np.isfinite(gain) else "-",
                               "%.0f" % f["ratio_p50"]],
                              widths, aligns))

    return "\n".join(lines)


def _ratio_headline(results, lang: str) -> str:
    """The ratios as one string, for the table's banner line."""
    if not results:
        return "-"
    parts = []
    for r in results:
        ratio = r["stats"]["ratio"]
        parts.append("%s %s" % (r["label"],
                                ("%.1fx" % ratio) if np.isfinite(ratio) else "-"))
    return ", ".join(parts)


def report(results, ref_xyz, source: str, rover: str = "", lang: str = "en") -> None:
    """Print the comparison table and the two notes that read it correctly.

    The notes are not decoration. The bias column is a datum artefact of how
    the base station's equations are built (see the module docstring) and the
    ratio is the only reference-independent number in the table; a reader given
    the numbers without them would draw the wrong conclusion from both.
    """
    width = 96
    print("=" * width)
    print("  " + t(lang, "rpt_rtk_title", rover=rover or "-"))
    print("  " + t(lang, "rpt_rtk_ref", x=ref_xyz[0], y=ref_xyz[1], z=ref_xyz[2],
                  src=source))
    n = results[0]["stats"]["n"] if results else 0
    print("  " + t(lang, "rpt_rtk_epochs", n=n, nmodes=len(results)))
    print("=" * width)
    print(comparison_table(results, lang))
    print("-" * width)
    # Note 3 only when there is a fixed solution to misread.
    if any(r.get("fix") for r in results):
        print("  " + t(lang, "rpt_rtk_note_fix"))
    print("  " + t(lang, "rpt_rtk_note_bias"))
    print("  " + t(lang, "rpt_rtk_note_ratio"))
    print("=" * width)
