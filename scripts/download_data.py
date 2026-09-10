#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Download the IGS products this project needs.

Public archives, no credentials. If a mirror is unreachable the script reports
which URLs it tried and moves on to the next, rather than failing the whole run
over one product.

    python scripts/download_data.py --product all --date 2025-01-01
    python scripts/download_data.py --product brdc        # just the nav file
    python scripts/download_data.py --list                # show what it fetches

Files land in data/. Existing files are skipped unless --force is given, so
re-running is cheap.

Security note: URLs are checked for embedded credentials before use, and the
script refuses a URL that carries a token or password. This is a public
repository and a committed token is a leaked token.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")

TIMEOUT = 60

# Mirrors in preference order. Each entry is (label, base URL).
GNSS_MIRRORS = [
    ("IGN France", "https://igs.ign.fr/pub/igs/data"),
    ("GSSC/ESA", "https://gssc.esa.int/gnss/data"),
    ("CDDIS", "https://cddis.nasa.gov/archive/gnss/data"),
]

# Products, as (directory template, filename template). {yyyy}, {doy}, {yy} are
# substituted from the requested date.
PRODUCTS = {
    "brdc": (
        "{yyyy}/brdc",
        "BRDC00IGS_R_{yyyy}{doy}0000_01D_MN.rnx.gz",
    ),
    "obs": (
        "{yyyy}/{doy}/{yy}{d}",
        "WUH200CHN_R_{yyyy}{doy}0000_01D_30S_MO.crx.gz",
    ),
    "sp3": (
        "{yyyy}/mgex/{doy}",
        "WUM0MGXFIN_{yyyy}{doy}0000_01D_05M_ORB.SP3.gz",
    ),
    "clk": (
        "{yyyy}/mgex/{doy}",
        "COD0MGXFIN_{yyyy}{doy}0000_01D_30S_CLK.CLK.gz",
    ),
}

LEAP_SECOND_URLS = [
    "https://data.iana.org/time-zones/data/leap-seconds.list",
    "https://hpiers.obspm.fr/iers/bul/bulc/Leap_Second.dat",
]


def _looks_credentialed(url: str) -> bool:
    low = url.lower()
    return any(k in low for k in ("token=", "password=", "api_key=", "apikey=",
                                  "access_key=", "secret="))


def _download(url: str, dest: str, force: bool = False) -> bool:
    if os.path.isfile(dest) and not force:
        print(f"  have  {os.path.basename(dest)}")
        return True

    if _looks_credentialed(url):
        print(f"  REFUSED  URL appears to carry a credential: {url}", file=sys.stderr)
        print("           Committing or sharing such a URL leaks it.", file=sys.stderr)
        return False

    print(f"  get   {os.path.basename(dest)}")
    print(f"        {url}")
    tmp = dest + ".part"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "gnsslab-download/1.0"})
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r, open(tmp, "wb") as f:
            total = 0
            while True:
                chunk = r.read(1 << 16)
                if not chunk:
                    break
                f.write(chunk)
                total += len(chunk)
        os.replace(tmp, dest)
        print(f"        {total/1e6:.2f} MB")
        return True
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError) as e:
        print(f"        failed: {e}")
        if os.path.exists(tmp):
            os.remove(tmp)
        return False


def _expand_gz(path: str) -> None:
    """Gunzip a .gz next to itself, then remove the archive."""
    import gzip
    import shutil
    out = path[:-3] if path.endswith(".gz") else path + ".out"
    with gzip.open(path, "rb") as fin, open(out, "wb") as fout:
        shutil.copyfileobj(fin, fout)
    os.remove(path)
    print(f"  unzip {os.path.basename(out)}")


def fetch_product(name: str, date: dt.date, force: bool) -> bool:
    tmpl_dir, tmpl_file = PRODUCTS[name]
    yyyy = date.strftime("%Y")
    doy = date.strftime("%j")
    yy = date.strftime("%y")
    d = date.strftime("%j").lstrip("0") or "0"

    subdir = tmpl_dir.format(yyyy=yyyy, doy=doy, yy=yy, d=d)
    fname = tmpl_file.format(yyyy=yyyy, doy=doy, yy=yy, d=d)
    dest = os.path.join(DATA, fname)

    print(f"\n[{name}] {fname}")
    for label, base in GNSS_MIRRORS:
        if _download(f"{base}/{subdir}/{fname}", dest, force):
            if dest.endswith(".gz"):
                try:
                    _expand_gz(dest)
                except OSError as e:
                    print(f"        could not expand: {e}", file=sys.stderr)
            return True
    print(f"  all mirrors failed for {name}", file=sys.stderr)
    return False


def fetch_leap_seconds(force: bool) -> bool:
    dest = os.path.join(DATA, "Leap_Second.dat")
    print("\n[leap-seconds] Leap_Second.dat")
    for url in LEAP_SECOND_URLS:
        if _download(url, dest, force):
            return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--product", default="solver",
                    choices=["all", "solver", "brdc", "obs", "sp3", "clk", "leap"],
                    help="'solver' = brdc + obs (default); 'all' = everything")
    ap.add_argument("--date", default="2025-01-01", help="YYYY-MM-DD (default 2025-01-01)")
    ap.add_argument("--force", action="store_true", help="re-download even if present")
    ap.add_argument("--list", action="store_true", help="show what would be fetched")
    args = ap.parse_args()

    if args.list:
        print("Products and their archive filenames:")
        for k, (d, f) in PRODUCTS.items():
            print(f"  {k:6s} {d}/{f}")
        print("  leap   Leap_Second.dat")
        return 0

    try:
        date = dt.datetime.strptime(args.date, "%Y-%m-%d").date()
    except ValueError:
        print(f"error: --date must be YYYY-MM-DD (got {args.date!r})", file=sys.stderr)
        return 2

    os.makedirs(DATA, exist_ok=True)

    want = {
        "solver": ["brdc", "obs"],
        "all": ["brdc", "obs", "sp3", "clk"],
    }.get(args.product, [args.product])

    print(f"Downloading to {DATA} for {date.isoformat()}")

    ok = 0
    for p in want:
        if fetch_product(p, date, args.force):
            ok += 1
    if args.product in ("all",):
        if fetch_leap_seconds(args.force):
            ok += 1

    total = len(want) + (1 if args.product == "all" else 0)
    print(f"\n{ok}/{total} downloaded.")

    if ok < total:
        print("\nSome products could not be fetched. The IGS mirrors reorganise\n"
              "their archives from time to time; see data/README.md for the\n"
              "data centres and the directory layout to fetch by hand.",
              file=sys.stderr)
        return 1

    print("Next: python scripts/make_sample_data.py --verify")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
