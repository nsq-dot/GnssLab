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
| `COD0MGXFIN_20250010000_01D_30S_CLK.CLK` | MGEX precise clock, 30 s | Used by `rtk --clk` (with `--sp3`); `SP3Store` carries the RINEX clock reader |
| `Leap_Second.dat` | IERS leap-second table | Time-system conversions |

`spp_if` needs only the first two. The SP3 and CLK are read by `rtk --sp3/--clk`
and by the auxiliary ephemeris programs.

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

## Chapter 8: the zero baseline and the WUM/OSB set

`tests/test_rtk_float_regression.py`, `test_rtk_mixed.py`,
`test_rtk_cycle_slips.py` and `test_rtk_osb.py` need the gitignored pair under
`Zero-baseline/` (2022-03-03, 1 Hz, two ~95 MB files). Their RINEX header
coordinates are 24.9 m from the antenna; `docs/rtk.md` §4 explains how the true
coordinate was established and why the files are left as the receiver reported
them.

The OSB measurements and `test_rtk_osb.py` additionally need the WHU/WUM
products for **2022 DOY 062** (2022-03-03), from
<https://data.bdsmart.cn/pub/product/gnss/WHU/2022/> (no credentials):

| File | Subdirectory |
|---|---|
| `WUM0MGXRAP_20220620000_01D_05M_ORB.SP3.gz` | `orbit/` |
| `WUM0MGXRAP_20220620000_01D_30S_CLK.CLK.gz` | `clock/` |
| `WUM0MGXRAP_20220620000_01D_01D_OSB.BIA.gz` | `bias/` |

All three must come from the same analysis center and day: the OSB values are
referred to that center's clock datum. The GFZ MGEX OSB file for the same day is
**not sufficient** for this program: it carries only the clock-reference signals
(GPS C1W/C2W, BeiDou C2I/C6I), while the BeiDou modes observe C2I+C7I and
C2I+C5P. The GFZ orbit and clock do load and GPS works with them, but without a
full OSB set the BDS columns get worse, not better — see `docs/rtk.md` §4.

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
