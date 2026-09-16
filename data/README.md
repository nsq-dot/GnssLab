# Data

Input data is **not committed to this repository** except for the small sample
described below. The full set is roughly 490 MB, including two ~65 MB raw
receiver logs and a 69 MB RINEX observation file, which is well past what
belongs in git.

## What is committed

```
data/
├── sample/                                       2.7 MB, committed
│   ├── WUH200CHN_R_20250010000_01D_30S_MO.rnx    63 epochs, 1.4 MB
│   └── BRDC00IGS_R_20250010000_01D_MN.rnx        trimmed navigation, 1.4 MB
└── README.md
```

The sample is trimmed from the full files to exactly the 63 epochs that
`tests/baseline/` was computed from, and is **verified to reproduce those
baselines byte-for-byte**. That verification is what makes it trustworthy as a
CI fixture and as the dataset behind `gnss demo`.

It contains observations and broadcast navigation only — **no SP3**, so
`bds_eph` and `bds_gps_diff` cannot be run from the sample alone. The smoke test
skips its ephemeris checks rather than failing when the SP3 is absent.

Regenerate it (after downloading the full set) with:

```bash
python scripts/make_sample_data.py --verify
```

The `--verify` step builds nothing by itself; run `gnss build` first, or it will
say so and skip the check.

## What to download

| File | Product | Notes |
|---|---|---|
| `WUH200CHN_R_20250010000_01D_30S_MO.rnx` | IGS daily observation, 30 s | Station WUH2, 2025-01-01 |
| `BRDC00IGS_R_20250010000_01D_MN.rnx` | IGS broadcast navigation, mixed | GPS + BeiDou, needed by the solver |
| `WUM0MGXFIN_20250010000_01D_05M_ORB.SP3` | MGEX precise orbit, 5 min | For `bds_eph` / `bds_gps_diff` |
| `COD0MGXFIN_20250010000_01D_30S_CLK.CLK` | MGEX precise clock, 30 s | Downloaded, but **no program reads it today** |
| `Leap_Second.dat` | IERS leap-second table | Time-system conversions |

`spp_if` needs only the first two. The rest are for the auxiliary programs — and
of those, only the SP3 is actually consumed: `src/Rx3ClockReader.*` exists and is
held by `SP3Store`, but no executable loads a `.CLK` file. The row is kept
because the download script fetches it and the intended use is clear.

`scripts/download_data.py` fetches these from public archives without
credentials:

```bash
python scripts/download_data.py --product all --date 2025-01-01
```

If automatic download fails (mirrors change, or you are behind a restricted
network), fetch them by hand from any IGS data centre and drop them into this
directory. Useful starting points:

- **Earthdata / CDDIS** — <https://cddis.nasa.gov/archive/gnss/>
- **IGN France** — <https://igs.ign.fr/pub/igs/data/>
- **GSSC / ESA** — <https://gssc.esa.int/gnss/data/daily/>

Daily directories are laid out as `<year>/<doy>/`, e.g. `2025/001/` for 1 January
2025.

## Station

The dataset is from **WUH2** (Wuhan, China):

| | |
|---|---|
| Marker | `21602M007` |
| Receiver | JAVAD TRE_3 |
| Approx. position (ECEF) | −2267749.000, 5009154.000, 3221290.000 m |
| Period | 2025-01-01 00:00:00 – 23:59:30 UTC, 30 s |

The approximate position is read from the RINEX header at run time; nothing
hardcodes it. **It is only metre-to-decametre accurate**, which is a real
limitation of the accuracy figures — see the caveat in the main README.

## Compressed files

IGS distributes observation files Hatanaka-compressed as `.crx` (or `.d` in the
short-name convention). Expand before use:

```bash
tools/CRZ2RNX.BAT <file>.crx        # Windows, from the repository root
```

The decompressor binaries are not redistributed here for licensing reasons; see
`tools/README.md` for how to obtain them. The repository does carry
`WUH200CHN_..._MO.rnx` already expanded, so this is only needed for new data.

## Licensing of the data

IGS products are openly available; cite the IGS or the originating agency
(GFZ, CNES, WHU) as appropriate. The raw receiver logs under `data/Zero-baseline/`
in a full local checkout originate from a NovAtel OEM719 and are not part of the
public dataset — they are excluded from the repository.
