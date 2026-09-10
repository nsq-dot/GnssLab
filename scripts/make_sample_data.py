#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build the small committed sample dataset from the full downloads.

The full WUH2 observation file is 69 MB and the broadcast navigation file is
15 MB. Neither belongs in git, but the repository still needs *some* data so a
reader can build and run `gnss demo` without downloading anything.

This trims both files down to the 63 epochs (31.5 minutes) that
`tests/baseline/` was computed from, and verifies that the trimmed pair
reproduces those baselines exactly before declaring success.

That verification is the whole point. A truncated RINEX file is only useful if
it gives the same answer, and RINEX records are self-contained enough that it
should - but "should" is not good enough for something that becomes both the
CI fixture and the first impression a reader gets.

Usage:
    python scripts/make_sample_data.py [--epochs 63] [--verify]

Requires the full dataset in data/; see data/README.md for where to get it.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")
SAMPLE = os.path.join(DATA, "sample")

OBS_NAME = "WUH200CHN_R_20250010000_01D_30S_MO.rnx"
NAV_NAME = "BRDC00IGS_R_20250010000_01D_MN.rnx"

# The baselines were computed from 00:00:00 to 00:30:30 inclusive, which is 63
# epochs at 30 s. The solver stops when epoch > stopUTC, so the 63rd epoch is the
# one at 00:30:30 and the sample must contain at least that many.
DEFAULT_EPOCHS = 63


def strip_trailing_ws(path: str) -> int:
    """Remove trailing whitespace from every line. Returns lines changed.

    The source files pad lines out with trailing spaces. Git flags trailing
    whitespace, and the diff of a sample file is something a reader may well
    look at, so normalising it is worth the one pass.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    changed = 0
    out = []
    for ln in lines:
        s = ln.rstrip()
        if s != ln:
            changed += 1
        out.append(s)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    return changed


def trim_obs(src: str, dst: str, epochs: int) -> tuple[int, int]:
    """Copy the header plus the first `epochs` observation epochs."""
    with open(src, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    header_end = None
    for i, ln in enumerate(lines):
        if "END OF HEADER" in ln:
            header_end = i
            break
    if header_end is None:
        raise RuntimeError(f"no END OF HEADER in {src}")

    out = lines[:header_end + 1]
    seen = 0
    for ln in lines[header_end + 1:]:
        if ln.startswith(">"):
            if seen >= epochs:
                break
            seen += 1
        out.append(ln)

    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(out)
    return len(out), seen


def _parse_rinex_epoch(line: str):
    """Parse the epoch field of a navigation record's first line.

    RINEX 3/4 broadcast records start with
    ``SV YYYY MM DD HH MM SS.SS ...``  Returns ``(sv, hour, minute)`` or None.
    """
    try:
        sv = line[:3].strip()
        parts = line[3:23].split()
        if len(parts) < 5:
            return None
        _y, _mo, _d, hh, mm = (int(parts[0]), int(parts[1]), int(parts[2]),
                               int(parts[3]), int(parts[4]))
        return sv, hh, mm
    except (ValueError, IndexError):
        return None


def trim_nav(src: str, dst: str, keep_svs: set[str] | None = None,
             hour_lo: int = 22, hour_hi: int = 4, minute_pad: int = 30) -> tuple[int, int]:
    """Trim a RINEX navigation file to whole ephemeris records.

    Records must be kept or dropped whole: half a record is an unparseable file
    with no indication why. RINEX 4 delimits records with a ``>`` line; RINEX
    3.04 - which is what IGS broadcast files use here - does not, so records are
    counted off in fixed 8-line groups, the first line carrying the SV id.

    Records are additionally filtered to a window around the observation period.
    A full day of broadcast navigation is ~2900 GPS records and 4.5 MB, of which
    a 31-minute run selects a small fraction, so the sample would otherwise carry
    megabytes of dead weight.

    The window deliberately spans midnight (22:00 the previous day through 04:30
    on the day of observation) rather than starting at 00:00. Ephemeris is
    *transmitted* up to two hours after its time-of-ephemeris, and the solver's
    ephemeris store reaches back before the first epoch, so records sent late on
    the previous evening are the ones it uses. Starting the window at midnight
    silently changes the solution - measured, not assumed.

    These bounds were found by bisection against the baseline, not derived, so
    the --verify step is the real guarantee. If you change them, re-run with
    --verify. ``minute_pad`` widens the window symmetrically.
    """
    with open(src, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    header_end = None
    for i, ln in enumerate(lines):
        if "END OF HEADER" in ln:
            header_end = i
            break
    if header_end is None:
        raise RuntimeError(f"no END OF HEADER in {src}")

    header = lines[:header_end + 1]
    body = lines[header_end + 1:]

    def in_window(hh: int, mm: int) -> bool:
        mins = hh * 60 + mm
        lo = hour_lo * 60 - minute_pad
        hi = hour_hi * 60 + minute_pad
        if lo <= hi:
            return lo <= mins <= hi
        # Window crosses midnight (e.g. 22:00 -> 04:00).
        return mins >= lo or mins <= hi

    if any(ln.startswith(">") for ln in body[:200]):
        # RINEX 4 style: records begin with '>'.
        records, cur = [], []
        for ln in body:
            if ln.startswith(">"):
                if cur:
                    records.append(cur)
                cur = [ln]
            else:
                cur.append(ln)
        if cur:
            records.append(cur)

        kept = []
        for rec in records:
            sv = rec[0][1:4].strip()
            if keep_svs and sv not in keep_svs:
                continue
            parsed = _parse_rinex_epoch(rec[0][1:])
            if parsed and not in_window(parsed[1], parsed[2]):
                continue
            kept += rec
        with open(dst, "w", encoding="utf-8", newline="\n") as f:
            f.writelines(header + kept)
        return len(header) + len(kept), len(kept)

    # RINEX 3 style: fixed 8-line records, no delimiter.
    kept = []
    n_rec = 0
    for i in range(0, len(body) - 7, 8):
        rec = body[i:i + 8]
        if not rec[0].strip():
            continue
        parsed = _parse_rinex_epoch(rec[0])
        if not parsed:
            continue
        sv, hh, mm = parsed
        if keep_svs and sv not in keep_svs:
            continue
        if not in_window(hh, mm):
            continue
        kept += rec
        n_rec += 1

    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(header + kept)
    return len(header) + len(kept), n_rec


def used_svs(obs_path: str) -> set[str]:
    """Every satellite id appearing in an observation file."""
    svs = set()
    with open(obs_path, "r", encoding="utf-8", errors="replace") as f:
        started = False
        for ln in f:
            if not started:
                if "END OF HEADER" in ln:
                    started = True
                continue
            if ln.startswith(">") or not ln.strip():
                continue
            sv = ln[:3].strip()
            if sv:
                svs.add(sv)
    return svs


def verify(sample_obs: str, sample_nav: str) -> bool:
    """Run the solver on the sample and diff against tests/baseline/."""
    exe = None
    for cand in (os.path.join(ROOT, "build", "bin", "spp_if.exe"),
                 os.path.join(ROOT, "build", "bin", "spp_if")):
        if os.path.isfile(cand):
            exe = cand
            break
    if not exe:
        print("  ! spp_if not built - skipping verification (run `gnss build` first)")
        return False

    out = os.path.join(ROOT, "output", "sample_check")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)

    cfg = os.path.join(ROOT, "config", "spp.ini")
    r = subprocess.run(
        [exe, cfg, "--obs", sample_obs, "--nav", sample_nav, "--out-dir", out],
        cwd=ROOT, capture_output=True, text=True,
    )
    if r.returncode != 0:
        print("  ! solver failed on the sample:")
        print("   ", (r.stderr or r.stdout).strip()[-500:])
        return False

    base_dir = os.path.join(ROOT, "tests", "baseline")
    pairs = [
        (os.path.join(out, os.path.basename(sample_obs) + "_DUAL_IF.spp.out"),
         os.path.join(base_dir, "WUH2_20250101_DUAL_IF.spp.out")),
        (os.path.join(out, os.path.basename(sample_obs) + "_pos_vel.out"),
         os.path.join(base_dir, "WUH2_20250101_pos_vel.out")),
    ]
    ok = True
    for got, want in pairs:
        if not os.path.isfile(got):
            print(f"  ! missing output: {got}")
            ok = False
            continue
        with open(got, "rb") as f1, open(want, "rb") as f2:
            if f1.read() == f2.read():
                print(f"  OK  {os.path.basename(got)} matches the baseline")
            else:
                print(f"  !   {os.path.basename(got)} DIFFERS from the baseline")
                ok = False
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--epochs", type=int, default=DEFAULT_EPOCHS,
                    help=f"observation epochs to keep (default {DEFAULT_EPOCHS})")
    ap.add_argument("--verify", action="store_true",
                    help="build and run on the sample, diffing against tests/baseline/")
    args = ap.parse_args()

    src_obs = os.path.join(DATA, OBS_NAME)
    src_nav = os.path.join(DATA, NAV_NAME)
    for p in (src_obs, src_nav):
        if not os.path.isfile(p):
            print(f"error: missing {p}\n"
                  "  Download the full dataset first - see data/README.md.", file=sys.stderr)
            return 1

    os.makedirs(SAMPLE, exist_ok=True)
    dst_obs = os.path.join(SAMPLE, OBS_NAME)
    dst_nav = os.path.join(SAMPLE, NAV_NAME)

    print(f"Trimming observation file to {args.epochs} epochs...")
    n_lines, n_epochs = trim_obs(src_obs, dst_obs, args.epochs)
    print(f"  {n_lines} lines, {n_epochs} epochs, {os.path.getsize(dst_obs)/1e6:.2f} MB")

    svs = used_svs(dst_obs)
    print(f"  satellites present: {len(svs)}")

    print("Trimming navigation file to the satellites actually observed...")
    n_lines, n_rec = trim_nav(src_nav, dst_nav, keep_svs=svs)
    print(f"  {n_lines} lines, {n_rec} ephemeris records, {os.path.getsize(dst_nav)/1e6:.2f} MB")

    for p in (dst_obs, dst_nav):
        changed = strip_trailing_ws(p)
        print(f"  normalised trailing whitespace on {changed} lines in {os.path.basename(p)}")

    total = os.path.getsize(dst_obs) + os.path.getsize(dst_nav)
    print(f"\nSample dataset: {total/1e6:.2f} MB total in {SAMPLE}")

    if args.verify:
        print("\nVerifying the sample reproduces tests/baseline/...")
        if verify(dst_obs, dst_nav):
            print("\nSample dataset verified.")
            return 0
        print("\nSample verification FAILED - do not commit this sample.", file=sys.stderr)
        return 1

    print("\nRun again with --verify to confirm it reproduces tests/baseline/.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
