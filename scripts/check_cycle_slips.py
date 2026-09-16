#!/usr/bin/env python3
"""Score a cycle-slip detector against injected ground truth.

Joins the manifest written by inject_cycle_slips.py to the detector's
per-satellite output and reports, per injected slip, whether it was detected -
and for the adversarial placements, whether the detector correctly declined to
make a judgement.

Also reports the detections that were *not* injected. On real data those are not
necessarily false alarms: the baseline observation file contains genuine slips
of its own. Use --baseline to point at a run on the un-injected file, so the two
counts can be differenced and the extra detections attributed to the injection.

The join and the scoring live in ``gnss_plot.cycleslip``, so the plotting layer
and this script cannot drift apart; this file is the command line and the
reporting. The numbers it prints for the 1 Hz and 30 s runs are quoted in
docs/cycle-slip-gf.md.

Usage:
    python scripts/check_cycle_slips.py --manifest truth.csv --run output/cs/injected
                                        [--mode diff|poly|mw] [--baseline output/cs/clean]
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "python", "src"))

from gnss_plot import cycleslip, io  # noqa: E402


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True, help="ground truth from the injector")
    ap.add_argument("--run", required=True, help="output directory of the injected run")
    ap.add_argument("--mode", choices=("diff", "poly", "mw"), default="diff")
    ap.add_argument("--baseline", default=None,
                    help="output directory of a run on the un-injected file")
    args = ap.parse_args()

    # Kept here rather than in the library: refusing to continue is the caller's
    # decision, and a library that calls SystemExit cannot be imported safely.
    if not cycleslip.detector_files(args.run, args.mode):
        raise SystemExit("error: no *%s files in %s"
                         % (cycleslip.FILENAME_SUFFIX[args.mode], args.run))

    truth = io.load_slip_manifest(args.manifest)
    run = cycleslip.load_detector_run(args.run, args.mode)
    baseline = (cycleslip.load_detector_run(args.baseline, args.mode)
                if args.baseline else None)
    score = cycleslip.score_injection(truth, run, baseline=baseline)

    print("detector: %s" % args.mode)
    print("run     : %s  (%d rows)" % (args.run, len(run)))
    print()

    header = "%-5s %-9s %-15s %10s %8s  %-9s %s" % (
        "sat", "yd", "label", "dN1,dN2", "dLI(m)", "status", "verdict")
    print(header)
    print("-" * len(header))

    for c in score["cases"]:
        # The cycle counts print through a float format rather than the manifest's
        # own text, so the table does not depend on how the injector spelled them.
        print("%-5s %-9s %-15s %5.0f,%-4.0f %+9.4f  %-9s %s" % (
            c["sat"], "%s/%s" % (c["year"], c["doy"]), c["label"],
            c["dN1"], c["dN2"], c["expected_dLI_m"],
            c["status"], c["verdict"]))

    print()
    tp, fn = score["tp"], score["fn"]
    print("injected slips to detect : %d" % (tp + fn))
    print("  detected (TP)          : %d" % tp)
    print("  missed   (FN)          : %d" % fn)
    if score["rate"] is not None:
        print("  detection rate         : %.1f%%" % (100.0 * score["rate"]))
    print("adversarial placements   : %d correct, %d wrong"
          % (score["adversarial_ok"], score["adversarial_bad"]))

    print()
    print("detections not injected  : %d  (of %d judged epochs = %.2f%%)"
          % (score["extra_detections"], score["tested"], 100.0 * score["extra_rate"]))

    if args.baseline:
        print("baseline run (%s): %d detections" % (args.baseline, score["baseline_slip"]))
        print("this run                : %d detections" % score["run_slip"])
        print("difference              : %+d  (injected %d, detected %d)"
              % (score["run_slip"] - score["baseline_slip"], tp + fn, tp))


if __name__ == "__main__":
    main()
