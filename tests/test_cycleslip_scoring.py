#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The MW detector has to score through the same path as the GF ones.

``cycleslip.load_detector_run`` reads a detector's last column as its verdict.
The GF detectors write OK/SLIP/INIT/GAP/WARMUP there, while MW writes 0/1 - the
translation is in ``cycleslip._STATUS_COLUMN``, and it is lossy on purpose,
because MW's flag does not separate an arc start or a post-gap epoch from a real
slip. That loss is a property of that detector and the chapter-7 document
reports it as such, which is why the adversarial ground-truth rows matter here:
they are what turns "MW reports more than it should" from an opinion into a
number.

The fixtures are written by this file, so no dataset and no build are needed.

    python tests/test_cycleslip_scoring.py
"""

from __future__ import annotations

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "python", "src"))

from gnss_plot import cycleslip, io  # noqa: E402

failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(f"  {'ok  ' if cond else 'FAIL'}  {msg}")
    if not cond:
        failures.append(msg)


# The header is load-bearing: io.load_gf_detector takes its column names from it,
# so the fixture has to carry the real one or the rows would not parse at all.
MW_HEADER = (
    "# sat year doy sod timeSystem mw_m meanMW_m csFlagArg flag\n"
    "# flag 1 = cycle slip reported at this epoch (includes the first\n"
    "# epoch of an arc and the first epoch after a data gap - the MW\n"
    "# detector does not separate those cases).\n"
)
GF_HEADER = (
    "# sat year doy sod timeSystem LI_m dLI_m meanDL_m sigmaDL_m flag status\n"
)


def write_mw(directory: str, sat: str, rows) -> None:
    """One satellite's ``<sat>.mw``: rows of (sod, mw, mean, flag)."""
    with open(os.path.join(directory, sat + ".mw"), "w") as f:
        f.write(MW_HEADER)
        for sod, mw, mean, flag in rows:
            f.write("%s 2025   1 %14.3f GPS %7.3f %7.3f %7.3f %d\n"
                    % (sat, sod, mw, mean, max(mw - mean, 0.0), flag))


def write_gf(directory: str, sat: str, rows) -> None:
    """One satellite's ``<sat>.gf.diff``: rows of (sod, stat, status)."""
    with open(os.path.join(directory, sat + ".gf.diff"), "w") as f:
        f.write(GF_HEADER)
        for sod, stat, status in rows:
            f.write("%s 2025   1 %14.3f GPS %10.3f %9.3f %9.3f %9.3f %d %s\n"
                    % (sat, sod, -10.0, stat, stat, 0.004, 0, status))


MANIFEST = """\
sat,year,doy,sod,label,dN1,dN2,expected_dLI_m,expect
G01,2025,1,0.000,arc-start,5,5,-0.269583,INIT
G01,2025,1,30.000,single-L1,1,0,0.190294,SLIP
G01,2025,1,60.000,equal-pair,5,5,0.000000,SLIP
"""


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        mw_dir = os.path.join(tmp, "mw")
        gf_dir = os.path.join(tmp, "gf")
        os.makedirs(mw_dir)
        os.makedirs(gf_dir)

        # sod 0 is the first epoch of an arc - structurally not a slip, but MW
        # reports it as one. sod 30 is a genuine single-band slip that both
        # detectors see, and sod 60 is an equal pair, which only GF can see.
        write_mw(mw_dir, "G01", [(0.0, 4.901, 4.901, 1),
                                 (30.0, 5.069, 4.985, 1),
                                 (60.0, 4.900, 4.900, 0),
                                 (90.0, 4.880, 4.890, 0)])
        write_mw(mw_dir, "G02", [(0.0, -6.100, -6.100, 1),
                                 (30.0, -6.090, -6.095, 0)])
        write_gf(gf_dir, "G01", [(0.0, 0.001, "INIT"),
                                 (30.0, 0.190, "SLIP"),
                                 (60.0, 0.539, "SLIP"),
                                 (90.0, 0.002, "OK")])

        files = cycleslip.detector_files(mw_dir, "mw")
        check(len(files) == 2, "detector_files finds both .mw files")
        check(all(p.endswith(".mw") for p in files),
              "the mw suffix is '.mw', not a gf one")

        run = cycleslip.load_detector_run(mw_dir, "mw")
        check(len(run) == 6, f"load_detector_run reads 6 rows (got {len(run)})")
        check(run.get(("G01", 2025, 1, 0.0)) == "SLIP",
              "a flag of 1 becomes SLIP")
        check(run.get(("G01", 2025, 1, 60.0)) == "OK",
              "a flag of 0 becomes OK")
        check(all(v in ("OK", "SLIP") for v in run.values()),
              "no MW row keeps its raw 0/1 token")

        gf_run = cycleslip.load_detector_run(gf_dir, "diff")
        check(gf_run.get(("G01", 2025, 1, 0.0)) == "INIT",
              "the GF path is untouched: INIT survives verbatim")

        both, gf_only, mw_only = cycleslip.compare_flagged(gf_run, run)
        check(both == {("G01", 2025, 1, 30.0)},
              "compare_flagged finds the one epoch both detectors report")
        check(gf_only == {("G01", 2025, 1, 60.0)},
              "the equal pair is GF-only")
        check(mw_only == {("G01", 2025, 1, 0.0), ("G02", 2025, 1, 0.0)},
              "both arc starts are MW-only, one per satellite")

        manifest_path = os.path.join(tmp, "truth.slips.csv")
        with open(manifest_path, "w") as f:
            f.write(MANIFEST)
        truth = io.load_slip_manifest(manifest_path)

        score = cycleslip.score_injection(truth, run)
        check(score["tp"] == 1, f"one injected slip detected (tp={score['tp']})")
        check(score["fn"] == 1, f"the equal pair is missed (fn={score['fn']})")
        check(score["rate"] == 0.5,
              f"detection rate is 1/2 (got {score['rate']})")

        # The arc-start row is the interesting one: MW cannot decline to judge,
        # so the row that exists to catch a false alarm catches MW's.
        verdicts = {str(c.get("label")): c for c in score["cases"]}
        check(verdicts["arc-start"]["detected"] is None,
              "the arc-start row is an adversarial case, not a detection")
        check(score["adversarial_bad"] == 1 and score["adversarial_ok"] == 0,
              "MW reports a slip at the arc start, which the row counts as wrong")
        check(score["tested"] == 6,
              f"all six judged epochs count as tested (got {score['tested']})")
        check(score["extra_detections"] == 1,
              f"one reported slip sits away from an injection "
              f"(got {score['extra_detections']})")

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED")
        return 1
    print("MW scoring reads and reports through the same path as GF.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
