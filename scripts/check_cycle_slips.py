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

Usage:
    python scripts/check_cycle_slips.py --manifest truth.csv --run output/cs/injected
                                        [--mode diff|poly] [--baseline output/cs/clean]
"""

import argparse
import csv
import glob
import os
import sys

# Column index of the status token in each detector's per-satellite output.
STATUS_COLUMN = {"diff": 10, "poly": 10}
FILENAME_SUFFIX = {"diff": ".gf.diff", "poly": ".gf.poly"}


def load_run(directory, mode):
    """Return {(sat, year, doy, sod): status} for one detector's output."""
    suffix = FILENAME_SUFFIX[mode]
    rows = {}
    files = sorted(glob.glob(os.path.join(directory, "*" + suffix)))
    if not files:
        raise SystemExit("error: no *%s files in %s" % (suffix, directory))

    status_column = STATUS_COLUMN[mode]
    for path in files:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if line.startswith("#") or not line.strip():
                    continue
                f = line.split()
                if len(f) <= status_column:
                    continue
                key = (f[0], int(f[1]), int(f[2]), round(float(f[3]), 3))
                rows[key] = f[status_column]
    return rows


def load_manifest(path):
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fh:
        return list(csv.DictReader(fh))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True, help="ground truth from the injector")
    ap.add_argument("--run", required=True, help="output directory of the injected run")
    ap.add_argument("--mode", choices=("diff", "poly"), default="diff")
    ap.add_argument("--baseline", default=None,
                    help="output directory of a run on the un-injected file")
    args = ap.parse_args()

    truth = load_manifest(args.manifest)
    run = load_run(args.run, args.mode)

    print("detector: %s" % args.mode)
    print("run     : %s  (%d rows)" % (args.run, len(run)))
    print()

    header = "%-5s %-9s %-15s %10s %8s  %-9s %s" % (
        "sat", "yd", "label", "dN1,dN2", "dLI(m)", "status", "verdict")
    print(header)
    print("-" * len(header))

    tp = fn = 0
    status_ok = status_bad = 0

    injected_keys = set()
    for row in truth:
        key = (row["sat"], int(row["year"]), int(row["doy"]), round(float(row["sod"]), 3))
        injected_keys.add(key)

        status = run.get(key, "MISSING")
        expect = row["expect"]

        if expect == "SLIP":
            if status == "SLIP":
                tp += 1
                verdict = "detected"
            else:
                fn += 1
                verdict = "MISSED"
        else:
            # The detector should have declined to judge here. Reporting a slip
            # is the specific failure this case exists to catch.
            if status == expect:
                status_ok += 1
                verdict = "correct"
            else:
                status_bad += 1
                verdict = "WRONG (expected %s)" % expect

        print("%-5s %-9s %-15s %5s,%-4s %+9.4f  %-9s %s" % (
            row["sat"], "%s/%s" % (row["year"], row["doy"]), row["label"],
            row["dN1"], row["dN2"], float(row["expected_dLI_m"]),
            status, verdict))

    print()
    print("injected slips to detect : %d" % (tp + fn))
    print("  detected (TP)          : %d" % tp)
    print("  missed   (FN)          : %d" % fn)
    if tp + fn:
        print("  detection rate         : %.1f%%" % (100.0 * tp / (tp + fn)))
    print("adversarial placements   : %d correct, %d wrong"
          % (status_ok, status_bad))

    # Detections away from any injected epoch. Not "false alarms" on real data -
    # the file has genuine slips - so the baseline run is what makes this number
    # interpretable.
    extras = [k for k, v in run.items() if v == "SLIP" and k not in injected_keys]
    tested = sum(1 for v in run.values() if v in ("OK", "SLIP"))
    print()
    print("detections not injected  : %d  (of %d judged epochs = %.2f%%)"
          % (len(extras), tested, 100.0 * len(extras) / tested if tested else 0.0))

    if args.baseline:
        base = load_run(args.baseline, args.mode)
        base_slip = sum(1 for v in base.values() if v == "SLIP")
        run_slip = sum(1 for v in run.values() if v == "SLIP")
        print("baseline run (%s): %d detections" % (args.baseline, base_slip))
        print("this run                : %d detections" % run_slip)
        print("difference              : %+d  (injected %d, detected %d)"
              % (run_slip - base_slip, tp + fn, tp))


if __name__ == "__main__":
    main()
