# Data formats

The interface between the C++ engine and the Python analysis layer is a pair of
plain text files. This document is the contract: both sides are written against
what is specified here.

## `<obsFileName>_<mode>.spp.out` — position

Written by `apps/spp_if.cpp` via `printSolution()`. **No header line.**

```
<year:4> <doy:3> <sod:14> <timeSystem> <X> <Y> <Z>
```

| Field | Meaning |
|---|---|
| `year` | 4-digit calendar year |
| `doy` | day of year, 1–366 |
| `sod` | second of day |
| `timeSystem` | literal `GPS` |
| `X`, `Y`, `Z` | ECEF position, metres, **fixed 3 decimals** |

Example:

```
2025   1              0 GPS  -2267753.119  5009158.927  3221298.572
2025   1             30 GPS  -2267754.466  5009161.919  3221298.955
```

## `<obsFileName>_pos_vel.out` — position and velocity

**One header line**, then one row per epoch:

```
sod X Y Z vE vN vU recClkDot
0.000000 -2267753.119335 5009158.926840 3221298.572055 -0.000899 0.000676 0.001062 1.979447
```

| Field | Meaning |
|---|---|
| `sod` | second of day |
| `X`, `Y`, `Z` | ECEF position, metres |
| `vE`, `vN`, `vU` | velocity in the local ENU frame, m/s |
| `recClkDot` | receiver clock drift as a range rate, m/s — divide by *c* for s/s |

All data fields are **fixed 6 decimals**.

Epochs where the velocity solution fails are still written, with `vE/vN/vU` and
`recClkDot` set to zero. Compare the epoch count in the manifest against the
number of rows to see whether that happened.

### Why `recClkDot` is in metres per second

The estimated parameter is `c·δṫ_r`, the clock drift scaled by the speed of
light, because that is the quantity that enters the range-rate equation
directly. Dividing by *c* = 299 792 458 m/s converts it to s/s. A typical
value of 1.6 m/s is about 5.4 × 10⁻⁹ s/s, which is an ordinary crystal
oscillator drift.

## The 0.000499 m cross-file difference

The same positions appear in both files at different precision. The maximum
absolute difference between them is **0.000499 m**, which is exactly the
half-step of the 3-decimal rounding in the `.spp.out` — not a discrepancy, and
not a sign the two files came from different runs. `tests/test_baseline_numerics.py`
asserts this, so a real inconsistency would be caught.

## `<obsFileName>_manifest.json`

Written by `apps/spp_if.cpp` alongside the two outputs:

```json
{
  "obs": "./data/WUH200CHN_R_20250010000_01D_30S_MO.rnx",
  "nav": "./data/BRDC00IGS_R_20250010000_01D_MN.rnx",
  "sppOut": "./output/WUH200CHN_R_20250010000_01D_30S_MO.rnx_DUAL_IF.spp.out",
  "posVelOut": "./output/WUH200CHN_R_20250010000_01D_30S_MO.rnx_pos_vel.out",
  "epochs": 63,
  "epochsWithVelocity": 63,
  "stopUTC": "2025-01-01T00:30:30",
  "cutOffElevation": 10,
  "runOnlyGPS": false,
  "runOnlyBDS": false,
  "tropEnabled": true,
  "bdsTgdEnabled": true
}
```

The Python layer reads this **first** and falls back to the filename convention
only if it is absent. That removes a whole class of bug: without it, a run with
different settings can silently be analysed against a stale output file that
happens to share the name.

Note the doubled extension in `sppOut`: output names are built by appending to
the *full* observation filename, so `....rnx` becomes `....rnx_DUAL_IF.spp.out`.
It reads oddly but is stable, and the manifest is the reliable way to find the
files.

## `<obsFileName>_<sys>_rtk_float.out` — RTK float solution

Written by `apps/rtk_float.cpp`, one line per epoch, in the format the textbook
uses:

```
2022  62          24517 GPS spp: -2267801.763  5009344.797  3220990.845 rtk: -2267812.520  5009352.076  3221012.151
```

| Columns | Contents |
|---|---|
| 0–3 | `YDSTime`: year, day of year, second of day, time system |
| 4–7 | literal `spp:`, then the rover's single-point ECEF position |
| 8–11 | literal `rtk:`, then the float solution's ECEF position |

Positions are at 3 decimals. `<sys>` is `gps`, `bds2` or `bds3` — see
[rtk.md](rtk.md) for why BeiDou needs two of them and neither covers the other.

The `spp:` column is **not** byte-reproducible across builds. The rover's
single-point solve carries one free ambiguity per phase equation, so its normal
matrix is singular in those directions and `inverse()` amplifies rounding noise;
the same build is self-consistent to the last digit, different builds differ by
metres. The `rtk:` column *is* stable, because the double-difference solve
re-references the position. `tests/test_rtk_float_regression.py` asserts the
strict column and tolerates the loose one, and explains both.

## `<obsFileName>_<sys>_rtk_diag.csv` — per-epoch diagnostics

```
sod,nRoverEq,nSD,nDD,nUnk,rank,cond,datumSat,datumFallback,nSDsats,absDxyz,sigma0,postfitRms
```

`rank` and `cond` are of the double-difference design matrix (`FullPivLU` and
`JacobiSVD`); `sigma0 = sqrt(Σ wᵢvᵢ² / (nObs − nUnk))` with `v = H·x − prefit` is
the post-fit residual. Nothing in the library checks the fit, so without these a
bad epoch is indistinguishable from a good one in the `.out` file. On the
zero-baseline set the two known outliers have `sigma0` 20–30× the median; see
[rtk.md](rtk.md) §6.

`datumFallback` is 1 when the reference satellite had to be taken from the
between-station system rather than from the rover's own highest-elevation
satellite (see `pickDatumSat` in `apps/rtk_float.cpp`). It is 0 on every epoch of
the shipped dataset.

## Naming

All paths are relative to the project root, except when overridden on the
command line (where the shell's working directory applies). `<mode>` is
`DUAL_IF` for the ionosphere-free combination, and the `--mode DUAL_RAW` option
changes the label without changing the `_pos_vel.out` name.

## Input files

| Format | Read by | Notes |
|---|---|---|
| RINEX observation (`.rnx`, `.obs`) | `src/RinexObsReader.cpp` | Versions 2.11 through 4.00 |
| RINEX navigation (`.rnx`) | `src/RinexNavStore.cpp` | GPS and BeiDou |
| SP3 precise orbit | `src/SP3Store.cpp` | Used by `bds_eph` / `bds_gps_diff` |
| CLK precise clock | `src/Rx3ClockReader.cpp` | Used by `bds_gps_diff` |

Hatanaka-compressed (`.crx` / `.d`) files must be expanded first — see
`tools/README.md`.
