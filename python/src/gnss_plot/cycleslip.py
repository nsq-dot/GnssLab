# -*- coding: utf-8 -*-
"""Cycle-slip detection: reading a whole detector run, and scoring it.

Chapter 7's programs write one file per satellite, so anything that wants to
talk about a *run* - how many slips were reported, whether the injected ones were
found - has to walk a directory and join what it finds to the ground truth. That
is analysis rather than parsing, which is why it lives here and not in
:mod:`gnss_plot.io`: ``io`` reads one file into arrays, this decides what the
numbers mean.

Two consumers share this module and nothing else: the ``gnss cs-plot`` command,
and ``scripts/check_cycle_slips.py``, which prints the injection scoreboard the
chapter-7 document quotes.
"""

from __future__ import annotations

import glob
import os

import numpy as np

from . import io

__all__ = [
    "STATE_ORDER",
    "FILENAME_SUFFIX",
    "detector_files",
    "load_detector_run",
    "mw_wavelength",
    "score_injection",
    "series_view",
]

#: The detector's judgement states, indexed by the integer the C++ writes in the
#: ``flag`` column. Only ``SLIP`` means a slip; the rest are reasons for *not*
#: judging this epoch (arc start, data gap, polynomial window still warming up).
#: See the state table in docs/cycle-slip-gf.md.
STATE_ORDER = ("OK", "SLIP", "INIT", "GAP", "WARMUP")

#: Per-satellite file suffix for each detector mode.
FILENAME_SUFFIX = {"diff": ".gf.diff", "poly": ".gf.poly", "mw": ".mw"}

#: How a mode's last column maps onto the two states the scorer compares
#: against. The GF detectors write a five-state token and need no translation.
#: MW writes a 0/1 flag, and this translation is deliberately blunt because the
#: flag is: apps/cs_detect_mw.cpp reports the first epoch of an arc and the first
#: epoch after a data gap as slips as well, since it does not separate those
#: cases. Against the adversarial ground-truth rows they therefore count as
#: false alarms - a property of that detector, not of this reader, and the
#: reason the GF detectors encode five states instead of a boolean.
_STATUS_COLUMN = {"mw": {"0": "OK", "1": "SLIP"}}

#: Epochs a detector was willing to judge, as opposed to the states above.
_JUDGED = ("OK", "SLIP")

#: Speed of light, m/s - the same value as C_MPS in src/Const.h.
_C_MPS = 299792458.0

#: The carrier frequencies the two combinations are formed from, mirroring
#: ``getFreq()`` in src/Const.h. That header is the source of truth; these are
#: copied rather than read because plotting runs without the C++ built.
#:
#: Keyed by (system, band), NOT by "first/second frequency". BeiDou's band
#: numbering is a trap here: the detectors take GPS L1/L2 but BeiDou **L2/L7**,
#: so BeiDou's first frequency is literally band "L2" (B1I, 1561.098 MHz).
#: Const.h also defines an L1_FREQ_BDS at 1575.420 MHz, but that is B1C and no
#: cycle-slip path ever selects it - a table keyed on "BeiDou L1" would quietly
#: take the wrong frequency and rescale every BeiDou series in the figures.
_FREQ = {
    ("G", "L1"): 1575.42e6,
    ("G", "L2"): 1227.60e6,
    ("C", "L2"): 1561.098e6,
    ("C", "L7"): 1207.140e6,
}

#: Which band pair each system forms the combination from, following
#: ``gfObsTypes()`` in src/GnssFunc.cpp and the identical selection in
#: ``detectCSMW``.
_MW_BANDS = {"G": ("L1", "L2"), "C": ("L2", "L7")}


def mw_wavelength(sat: str) -> float:
    """Wide-lane wavelength ``c/(f1-f2)`` for one satellite, in metres.

    `sat` is an identifier such as ``"G08"`` or ``"C10"``; only the system
    letter matters. GPS gives 0.8619 m and BeiDou B1I/B2b 0.8470 m.

    Dividing the MW combination by this yields the wide-lane ambiguity
    ``N_W = N1 - N2``, an integer while the phase is tracked continuously. That
    is the quantity the chapter's injection tables, its blind-spot analysis and
    its ``dN_W`` columns are written in, and the reason the MW figures plot
    cycles rather than the metres the detector file stores.

    Raises rather than falling back to 1.0: a silent default would rescale one
    constellation's entire series into a figure that still looked plausible.
    """
    system = sat[0].upper()
    bands = _MW_BANDS.get(system)
    if bands is None:
        raise ValueError(
            "no MW combination is defined for system %r (satellite %r); the "
            "detectors only form one for GPS and BeiDou" % (system, sat))
    return _C_MPS / (_FREQ[(system, bands[0])] - _FREQ[(system, bands[1])])


def _translate_status(mode: str, raw) -> str:
    """Put one detector row's last-column token into the scorer's vocabulary."""
    table = _STATUS_COLUMN.get(mode)
    return table.get(str(raw), str(raw)) if table else str(raw)


def detector_files(directory: str, mode: str = "diff") -> list[str]:
    """Sorted per-satellite files of one detector in `directory`.

    Returns an empty list rather than raising when there are none: whether that
    is fatal depends on the caller, and a library should not decide to abort a
    process.
    """
    return sorted(glob.glob(os.path.join(directory, "*" + FILENAME_SUFFIX[mode])))


def load_detector_run(directory: str, mode: str = "diff") -> dict[tuple, str]:
    """Read a whole detector run into ``{(sat, year, doy, sod): status}``.

    The key is what the injected-slips manifest carries, so the two join
    directly. ``sod`` is rounded to three decimals, matching the precision the
    injector writes, so a value that survives a round trip through text still
    matches.

    A row whose time fields do not parse is skipped - ``int(nan)`` raises
    ``ValueError``, not ``TypeError``, so the tolerant-parse guard has to cover
    both the conversion and the ``nan`` case.

    ``mode`` also decides how the file's last column is read: the GF detectors
    already write OK/SLIP/INIT/GAP/WARMUP, while MW writes 0/1 and goes through
    :data:`_STATUS_COLUMN`. See the note there for why MW's flag cannot be
    translated more finely than it is.
    """
    rows: dict[tuple, str] = {}
    for path in detector_files(directory, mode):
        _sat, cols, status = io.load_gf_detector(path)
        year = cols.get("year")
        doy = cols.get("doy")
        sod = cols.get("sod")
        if year is None or doy is None or sod is None:
            continue
        sat_col = cols.get("sat")
        for i in range(status.size):
            try:
                key = (
                    str(sat_col[i]) if sat_col is not None else _sat,
                    int(year[i]), int(doy[i]), round(float(sod[i]), 3),
                )
            except (ValueError, TypeError, OverflowError):
                continue
            rows[key] = _translate_status(mode, status[i])
    return rows


def compare_flagged(run_a: dict, run_b: dict):
    """Split two detectors' reported slips into the shared and the private ones.

    Returns ``(both, a_only, b_only)`` as sets of the ``(sat, year, doy, sod)``
    keys :func:`load_detector_run` produces. "Reported" means SLIP and nothing
    else: a state only one detector has - MW reports no INIT, GAP or WARMUP - is
    not a report either way.

    This is the cross-validation in one function. Two combinations with different
    null spaces should agree on real slips and disagree exactly where one of them
    is blind, so the two private sets are the interesting output rather than the
    shared one.
    """
    a = {k for k, v in run_a.items() if v == "SLIP"}
    b = {k for k, v in run_b.items() if v == "SLIP"}
    return a & b, a - b, b - a


def score_injection(manifest, run: dict | None = None, baseline: dict | None = None):
    """Score a detector run against the injected ground truth.

    `manifest` is either the nine-array tuple from
    :func:`gnss_plot.io.load_slip_manifest` or a path to the file. `run` is a
    mapping from :func:`load_detector_run`; `baseline` is the same for a run on
    the *un-injected* file, used to attribute detections to the injection rather
    than to the genuine slips the real data already contains.

    Returns a dict:

    ``cases``
        one record per manifest row, in file order, each carrying the manifest
        fields, the ``status`` the detector reported (``"MISSING"`` if the epoch
        is absent from the run), a human-readable ``verdict``, and ``detected``
        (True/False/None - None for an adversarial case, which is not a
        detection question).
    ``tp``, ``fn``, ``rate``
        over the rows that expect ``SLIP``; ``rate`` is None when there are none.
    ``adversarial_ok``, ``adversarial_bad``
        rows where the detector should have *declined* to judge. Reporting a slip
        there is the specific failure those cases exist to catch.
    ``tested``, ``extra_detections``, ``extra_rate``
        slips reported away from any injected epoch, of all judged epochs.
    ``baseline_slip``, ``run_slip``
        total SLIP counts, only when `baseline` is given.

    The verdict strings are part of the output contract: ``check_cycle_slips.py``
    prints them verbatim.
    """
    if isinstance(manifest, (str, os.PathLike)):
        manifest = io.load_slip_manifest(str(manifest))
    if run is None:
        run = {}

    sat, year, doy, sod, label, dn1, dn2, edli, expect = manifest

    cases = []
    tp = fn = 0
    adv_ok = adv_bad = 0
    injected_keys = set()

    for i in range(np.asarray(sat).size):
        key = (str(sat[i]), int(year[i]), int(doy[i]), round(float(sod[i]), 3))
        injected_keys.add(key)

        status = run.get(key, "MISSING")
        want = str(expect[i])

        if want == "SLIP":
            if status == "SLIP":
                tp += 1
                verdict, detected = "detected", True
            else:
                fn += 1
                verdict, detected = "MISSED", False
        else:
            # The detector should have declined to judge here.
            if status == want:
                adv_ok += 1
                verdict = "correct"
            else:
                adv_bad += 1
                verdict = "WRONG (expected %s)" % want
            detected = None

        cases.append({
            "sat": str(sat[i]),
            "year": int(year[i]),
            "doy": int(doy[i]),
            "sod": float(sod[i]),
            "label": str(label[i]),
            "dN1": float(dn1[i]),
            "dN2": float(dn2[i]),
            "expected_dLI_m": float(edli[i]),
            "expect": want,
            "status": status,
            "verdict": verdict,
            "detected": detected,
        })

    extras = sum(1 for k, v in run.items() if v == "SLIP" and k not in injected_keys)
    tested = sum(1 for v in run.values() if v in _JUDGED)

    out = {
        "cases": cases,
        "tp": tp,
        "fn": fn,
        "rate": (tp / (tp + fn)) if (tp + fn) else None,
        "adversarial_ok": adv_ok,
        "adversarial_bad": adv_bad,
        "tested": tested,
        "extra_detections": extras,
        "extra_rate": (extras / tested) if tested else 0.0,
        "baseline_slip": None,
        "run_slip": sum(1 for v in run.values() if v == "SLIP"),
    }

    if baseline is not None:
        out["baseline_slip"] = sum(1 for v in baseline.values() if v == "SLIP")

    return out


def series_view(path: str, kind: str):
    """Project one detector file onto the five arrays a figure needs.

    Returns ``(sod, li, stat, flag, status)`` where ``stat`` is the quantity the
    detector thresholds: ``|dLI|`` for the epoch-difference detector, ``|resid|``
    for the polynomial one. Absolute values because the figure puts both
    detectors on one log axis against one threshold line, and a log axis cannot
    show a sign.

    `kind` selects the statistic column; the file's own header supplies it, so
    the only thing this needs from the caller is which detector it is.

    ``kind="mw"`` returns the MW combination in place of ``L_I`` and, as the
    statistic, the bias that combination had against the mean the detector was
    carrying in - which is what it compares its two thresholds against. MW
    writes no residual column of its own, so the bias is reconstructed here
    rather than read, from the current combination and the previous epoch's
    stored mean; its first element is NaN, because the first epoch has no mean
    carried in and is flagged for a different reason (no previous epoch to
    difference against). The figure then has the same three quantities for MW as
    for the two GF detectors.

    MW is the one case where the returned value is **not** in the file's units:
    the ``.mw`` file stores metres, and both the combination and its deviation
    are divided by the wide-lane wavelength here so that ``li`` is the
    wide-lane ambiguity ``N_W`` in cycles (see `mw_wavelength`). The two GF
    kinds are left in metres, because the GF combination is not an integer
    multiple of any wavelength and carries an ionospheric term that is natively
    metres - labelling it "cycles" would imply a periodicity it does not have.
    """
    sat, cols, status = io.load_gf_detector(path)

    def col(name):
        return np.asarray(cols.get(name, np.full(status.size, np.nan)), dtype=float)

    sod = col("sod")
    if kind == "mw":
        # Metres on disk, cycles on the axis: dividing by the wide-lane
        # wavelength puts the combination on the same scale as the detector's
        # own threshold, which is written as minCycles(2.0) x wavelengthMW.
        scale = mw_wavelength(sat)
        mw = col("mw_m")
        mean = col("meanMW_m")
        li = mw / scale

        # The bias the detector tested is measured against the mean it is
        # carrying IN, which is the value the file holds for the PREVIOUS epoch:
        # `meanMW_m` is written after the update, and a detected slip resets it
        # to the combination just read - so subtracting the stored mean of the
        # same epoch gives zero at exactly the epochs that were flagged. Shifting
        # by one recovers the tested bias, and reproduces every decision the
        # detector made (see docs/cycle-slip-gf.md).
        stat = np.full(mw.size, np.nan)
        stat[1:] = np.abs(mw[1:] - mean[:-1]) / scale
        status = np.asarray([_translate_status(kind, s) for s in status])
    else:
        li = col("LI_m")
        stat = np.abs(col("dLI_m" if kind == "diff" else "resid_m"))
    flag = col("flag")
    return sod, li, stat, flag, status
