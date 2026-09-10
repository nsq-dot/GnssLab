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
