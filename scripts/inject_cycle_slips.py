#!/usr/bin/env python3
"""Inject known cycle slips into a RINEX observation file.

Exercise 2 of chapter 7: the only way to say anything about a cycle-slip
detector's detection rate and false-alarm rate is to know where the slips are.
This writes a copy of an observation file with a fixed, documented set of
integer-cycle slips added to the carrier phase, plus a manifest giving the
ground truth.

Design rules, all of which matter for the result to be usable:

* **Deterministic.** The slip table is computed from the file, never randomised,
  so two runs produce byte-identical output. Everything the detector will be
  scored against is written to the manifest.
* **Integer cycles on the phase only.** The file stores phase in cycles; the C++
  reader converts to metres on load, so adding N to a phase field is an N-cycle
  slip by the time the detector sees it.
* **Every phase code of the band is modified.** The reader collapses observation
  codes to their first two characters and keeps them in a ``std::map``, so when
  a band carries several codes the alphabetically last one wins and the rest are
  discarded. Injecting into only one code would be invisible to the detector.
* **Nothing else is touched.** Header and unmodified records are copied verbatim.

Usage:
    python scripts/inject_cycle_slips.py --obs <in.obs> --out <out.obs>
                                         [--manifest <truth.csv>] [--plan auto|sample]

Then run the detector on the output and score it with check_cycle_slips.py.
"""

import argparse
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import rinex_obs  # noqa: E402

# Wavelengths [m] of the bands the detector uses, so the manifest can state the
# expected response of the geometry-free combination.
WAVELENGTH = {
    "G": {"1": 0.190293673, "2": 0.244210213},
    "C": {"2": 0.192039484, "7": 0.248349397},
}

# (band for L1Type, band for L2Type) per constellation, matching gfObsTypes() in
# src/GnssFunc.cpp: GPS on L1/L2, BDS on B1I/B2I which RINEX spells band 2/7.
DETECTOR_BANDS = {
    "G": ("1", "2"),
    "C": ("2", "7"),
}

# The slips to inject. Each is (label, delta N on the first band, delta N on the
# second band) in whole cycles.
#
# The set is chosen so that a run says something specific:
#   min-cycle-pair  (1,1) - the smallest slip the combination can resolve,
#                           |lambda1 - lambda2| = 5.4 cm. This is the textbook's
#                           hard case and it is what separates a detector that
#                           works from one that only looks like it does.
#   single-L1/L2    the two one-sided cases, which also pin down the sign.
#   large-pair      well clear of any threshold; must never be missed.
#   negative-pair   the same magnitude going the other way.
#   gf-null-space   (77,60) for GPS gives lambda1*77 - lambda2*60 = 0.000 m
#                   exactly. The geometry-free combination is provably blind to
#                   it; the detector *must* miss it, and a run that "detects" it
#                   is broken.
CASES = [
    ("min-cycle-pair", 1, 1),
    ("single-L1", 1, 0),
    ("single-L2", 0, 1),
    ("large-pair", 10, 10),
    ("negative-pair", -4, -4),
    ("gf-null-space", 77, 60),
]

# Slips for the small 63-epoch sample file, where there is not room for six
# spread-out cases plus the adversarial ones.
SAMPLE_CASES = [
    ("min-cycle-pair", 1, 1),
    ("single-L1", 1, 0),
    ("large-pair", 10, 10),
]


def scan(path):
    """One pass over the file: injectable epochs per satellite.

    "Injectable" means the satellite is present at that epoch *and* both bands
    the detector uses carry phase. Injecting where a field is blank would create
    a value out of nothing, which is not a cycle slip.

    Returns (obs_types, {sat: [epoch_index, ...]}, {epoch_index: timestamp}).
    """
    header, obs_types, _ = rinex_obs.read_header(path)

    bands_by_system = {}
    for system, types in obs_types.items():
        if system not in DETECTOR_BANDS:
            continue
        bands = rinex_obs.phase_bands(types)
        b1, b2 = DETECTOR_BANDS[system]
        if b1 in bands and b2 in bands:
            bands_by_system[system] = bands

    index_of = {system: {o: i for i, o in enumerate(types)}
                for system, types in obs_types.items()}

    present = {}
    timestamps = {}
    epoch_index = -1

    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fh:
        # Skip the header, which read_header has already consumed by line count.
        for _ in range(len(header)):
            next(fh)

        for line in fh:
            if not line.startswith(">"):
                continue
            epoch_index += 1
            timestamps[epoch_index] = rinex_obs.parse_epoch_time(line)
            flag, num_sat = rinex_obs.parse_epoch_line(line)

            for _ in range(num_sat):
                record = next(fh)
                if flag not in (0, 1, 6):
                    continue

                sat = rinex_obs.satellite_of(record)
                system = sat[0:1]
                if system not in bands_by_system:
                    continue

                bands = bands_by_system[system]
                b1, b2 = DETECTOR_BANDS[system]
                idx = index_of[system]

                ok = all(rinex_obs.get_value(record, idx[o]) is not None
                         for band in (b1, b2) for o in bands[band])
                if ok:
                    present.setdefault(sat, []).append(epoch_index)

    return obs_types, present, timestamps


def build_plan(present, cases, min_gap_epochs):
    """Turn visibility into a list of slip entries.

    Returns a list of dicts with the injection, plus a separate list for the
    adversarial placements, which need to be checked against the *status* the
    detector reports rather than against a simple "was it flagged" test.
    """
    ranked = sorted(present.items(), key=lambda kv: (-len(kv[1]), kv[0]))
    if not ranked:
        raise SystemExit("error: no satellite carries both detector bands")

    entries = []
    used = set()

    for n, (label, dn1, dn2) in enumerate(cases):
        sat, epochs = ranked[n % len(ranked)]
        # Spread the cases across the arc rather than clustering them.
        frac = (n + 1) / (len(cases) + 1)
        epoch = epochs[int(frac * (len(epochs) - 1))]
        while (sat, epoch) in used and epoch + 1 in epochs:
            epoch += 1
        used.add((sat, epoch))
        entries.append({"sat": sat, "epoch_index": epoch, "dN1": dn1, "dN2": dn2,
                        "label": label, "expect": "SLIP"})

    # Adversarial 1: a slip at the very first epoch the satellite is seen. The
    # detector has nothing to difference against there, so the correct answer is
    # "no judgement" (INIT), not "slip". A detector that reports a slip here is
    # wrong, and it is a mistake the MW example program makes on every satellite.
    sat, epochs = ranked[0]
    entries.append({"sat": sat, "epoch_index": epochs[0], "dN1": 5, "dN2": 5,
                    "label": "arc-start", "expect": "INIT"})

    # Adversarial 2: the first epoch after the largest interruption of tracking.
    # The ambiguity restarts across the gap, so the answer is again "no
    # judgement" (GAP).
    best = None
    for sat, epochs in ranked:
        for a, b in zip(epochs, epochs[1:]):
            if b - a > min_gap_epochs and (best is None or b - a > best[0]):
                best = (b - a, sat, b)
    if best is not None:
        _, sat, epoch = best
        if (sat, epoch) not in used:
            used.add((sat, epoch))
            entries.append({"sat": sat, "epoch_index": epoch, "dN1": 3, "dN2": 3,
                            "label": "after-gap", "expect": "GAP"})

    # Adversarial 3: equal and opposite slips one epoch apart. A common
    # misconception is that these cancel and go unnoticed; the statistic is a
    # magnitude of a single epoch difference, so both epochs must be flagged.
    # Start from mid-arc, not from the first epochs: a slip placed before the
    # detector has any reference is simply absorbed into the arc's anchor value,
    # which is not what this case is meant to demonstrate.
    sat, epochs = ranked[1 % len(ranked)]
    for i in range(len(epochs) // 2, len(epochs) - 1):
        if epochs[i + 1] == epochs[i] + 1 and (sat, epochs[i]) not in used:
            entries.append({"sat": sat, "epoch_index": epochs[i], "dN1": 6, "dN2": 6,
                            "label": "opposed-a", "expect": "SLIP"})
            entries.append({"sat": sat, "epoch_index": epochs[i + 1], "dN1": -6, "dN2": -6,
                            "label": "opposed-b", "expect": "SLIP"})
            used.add((sat, epochs[i]))
            used.add((sat, epochs[i + 1]))
            break

    return entries


def inject(path, out_path, entries, obs_types):
    """Second pass: copy the file, applying the slips on the way through."""
    index_of = {system: {o: i for i, o in enumerate(types)}
                for system, types in obs_types.items()}
    bands_by_system = {}
    for system, types in obs_types.items():
        if system in DETECTOR_BANDS:
            bands_by_system[system] = rinex_obs.phase_bands(types)

    by_epoch = {}
    for e in entries:
        by_epoch.setdefault(e["epoch_index"], {})[e["sat"]] = e

    applied = []
    header_lines, _, _ = rinex_obs.read_header(path)

    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fin, \
            open(out_path, "w", encoding="utf-8", newline="\n") as fout:
        for _ in range(len(header_lines)):
            fout.write(next(fin))

        epoch_index = -1
        for line in fin:
            if not line.startswith(">"):
                continue
            epoch_index += 1
            flag, num_sat = rinex_obs.parse_epoch_line(line)
            fout.write(line)

            edits = by_epoch.get(epoch_index, {})
            for _ in range(num_sat):
                record = next(fin).rstrip("\n").rstrip("\r")

                if flag in (0, 1, 6) and rinex_obs.satellite_of(record) in edits:
                    entry = edits[rinex_obs.satellite_of(record)]
                    system = entry["sat"][0:1]
                    b1, b2 = DETECTOR_BANDS[system]
                    bands = bands_by_system[system]
                    idx = index_of[system]

                    for band, delta in ((b1, entry["dN1"]), (b2, entry["dN2"])):
                        for obs in bands[band]:
                            value = rinex_obs.get_value(record, idx[obs])
                            if value is None:
                                continue
                            record = rinex_obs.set_value(record, idx[obs], value + delta)

                    applied.append(entry)

                fout.write(record + "\n")


def day_of_year(year, month, day):
    """Day of year, so the manifest lines up with the detector's YD time."""
    import datetime
    return datetime.date(year, month, day).timetuple().tm_yday


def expected_dli(sat, dn1, dn2):
    """Response of L_I = L1 - L2 to the injected slip, in metres."""
    system = sat[0:1]
    b1, b2 = DETECTOR_BANDS[system]
    return WAVELENGTH[system][b1] * dn1 - WAVELENGTH[system][b2] * dn2


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--obs", required=True, help="input RINEX observation file")
    ap.add_argument("--out", required=True, help="output file with slips injected")
    ap.add_argument("--manifest", default=None,
                    help="ground-truth CSV (default: <out>.slips.csv)")
    ap.add_argument("--plan", choices=("auto", "sample"), default="auto",
                    help="auto: the full case table; sample: three cases, for the "
                         "short 63-epoch sample file")
    ap.add_argument("--gap-seconds", type=float, default=120.0,
                    help="gap treated as an interruption, must match deltaTMax (s)")
    args = ap.parse_args()

    manifest = args.manifest or (args.out + ".slips.csv")

    obs_types, present, timestamps = scan(args.obs)
    if not present:
        raise SystemExit("error: no injectable satellite found in %s" % args.obs)

    cases = SAMPLE_CASES if args.plan == "sample" else CASES

    # A gap in the epoch index only means lost tracking if it is long enough for
    # the detector to call it an interruption; convert the time threshold using
    # the file's own median sampling interval.
    epoch_span = max(1, max(timestamps) - min(timestamps))
    n_epochs = len(timestamps)
    step = epoch_span / max(1, n_epochs - 1)
    min_gap_epochs = args.gap_seconds / step

    entries = build_plan(present, cases, min_gap_epochs)

    inject(args.obs, args.out, entries, obs_types)

    with open(manifest, "w", encoding="utf-8", newline="\n") as fh:
        writer = csv.writer(fh)
        # `year`, `doy` and `sod` are the columns the detector writes, so the
        # ground truth joins to its output without either side re-deriving a
        # calendar date.
        writer.writerow(["sat", "year", "doy", "sod", "label", "dN1", "dN2",
                         "expected_dLI_m", "expect"])
        for e in sorted(entries, key=lambda x: (x["epoch_index"], x["sat"])):
            ts = timestamps[e["epoch_index"]]
            # RINEX splits the epoch into h/m/s; the detector reports seconds of
            # day, so convert rather than emitting the seconds field alone.
            sod = ts[3] * 3600 + ts[4] * 60 + ts[5]
            writer.writerow([e["sat"], ts[0], day_of_year(*ts[:3]), "%.3f" % sod,
                             e["label"], e["dN1"], e["dN2"],
                             "%.6f" % expected_dli(e["sat"], e["dN1"], e["dN2"]),
                             e["expect"]])

    print("injected %d slip(s) into %s" % (len(entries), args.out))
    print("ground truth: %s" % manifest)
    for e in sorted(entries, key=lambda x: (x["epoch_index"], x["sat"])):
        print("  %-5s epoch %6d  %-15s dN=(%4d,%4d)  dLI=%+9.4f m  expect %s"
              % (e["sat"], e["epoch_index"], e["label"], e["dN1"], e["dN2"],
                 expected_dli(e["sat"], e["dN1"], e["dN2"]), e["expect"]))


if __name__ == "__main__":
    main()
