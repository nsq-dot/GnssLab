# Configuration reference

Configuration files use `key = value`, one per line.

A few formatting rules cause most of the confusion:

- **A line is a comment only when `#` is its first character.** An inline
  trailing comment is *not* stripped: `cutOffElevation = 15  # wider mask`
  parses as the value `15  # wider mask`, which then fails to convert. Put
  comments on their own line.
- **Every key is optional.** Anything absent keeps the built-in default, so a
  three-line file is valid. A malformed value in an optional setting also falls
  back to the default rather than aborting the run.
- **A list is one comma-separated line.** The reader has no list type, so a
  value like `eph.ini`'s `satList` is a single string that the program splits.
  Tokens are trimmed and blank ones ignored, so a trailing comma is harmless.

Relative paths resolve against the **project root** (the parent of this
directory), not the current working directory, so a config works from anywhere.
A path given on the command line instead resolves against the shell's working
directory.

The config file is looked for under its default name (`config/spp.ini`,
`config/cs.ini`, `config/bias.ini`, `config/eph.ini`) in the working directory
and then in each parent directory in turn, so a run started from the build tree
— which is what an IDE does — still finds it. A config file named explicitly on
the command line is not searched for.

## Profiles

| File | Purpose |
|---|---|
| `spp.ini` | SPP + Doppler velocity (`spp_if`). Reproduces `tests/baseline/`. Use this unless you have a reason not to. |
| `spp.tuned.ini` | A wider elevation mask and a tighter code sigma. Deliberately does **not** reproduce the baseline. |
| `cs.ini` | Cycle-slip detection (`cs_detect_mw`, `cs_detect_gf`). Its keys are documented inline in that file. |
| `bias.ini` | Systematic-bias and RINEX-inventory diagnostics (`system_bias`, `read_rinex`). |
| `eph.ini` | Broadcast-versus-precise ephemeris comparison (`bds_eph`, `bds_gps_diff`). |

> **Do not change `cutOffElevation` in `spp.ini` to 15**, and do not add a
> `noiseGPSCode` override there. `SPPIFCode`'s constructor defaults to a 10°
> mask and a 1.0 m code sigma, and the baseline was produced with those. The
> observation weight is `1/sigma²`, so setting the sigma to 0.3 multiplies every
> weight by about 11×. Either change will move the solution and break the
> regression test — which will look like a code bug and is not one. Put
> experiments in `spp.tuned.ini`.

## Keys

### `spp.ini` — consumed

| Key | Type | Default | Effect |
|---|---|---|---|
| `obsFile` | path | `data/WUH200CHN_R_20250010000_01D_30S_MO.rnx` | RINEX observation file |
| `navFile` | path | `data/BRDC00IGS_R_20250010000_01D_MN.rnx` | RINEX broadcast navigation file |
| `outDir` | path | `output` | Output directory; created if absent |
| `stopUTC` | ISO 8601 | `2025-01-01T00:30:30` | Stop after this epoch. Empty means run to the end of the file |
| `GPS` | bool | `1` | Include GPS observations |
| `BD2` / `BD3` | bool | `1` | Include BeiDou observations |
| `cutOffElevation` | int (deg) | `10` | Elevation mask |
| `tropModel` | int | `1` | Tropospheric model; `0` disables the correction |
| `enableBDSTGD` | bool | `1` | Apply the BeiDou broadcast TGD correction |
| `obsModel` | int | `1` | `1` = ionosphere-free code combination (the only mode implemented) |
| `noiseGPSCode` | double (m) | `1.0` | Code sigma for the ionosphere-free combination; feeds `setSigIFCode()` |

`outFile` is also accepted, for compatibility with config files written against
the older placeholder ini; only its directory is used, since a run now produces
several files.

### `spp.ini` — reserved

Parsed and round-tripped, but **no current code consumes them**. They are kept
so that config files stay forward-compatible and so the intended settings are
recorded rather than lost. Setting them has no effect today.

| Key | Type | Intended meaning |
|---|---|---|
| `ionoModel` | int | `1` Klobuchar, `2` GIM — Klobuchar only, and only on the single-frequency path |
| `minSatNum` | int | Minimum satellites for a solution |
| `maxGDOP` | int | Maximum geometric dilution of precision |
| `noiseBD2Code`, `noiseBD3Code` | double (m) | Per-constellation code sigma. The solver carries one scalar sigma for the ionosphere-free combination, so these are not applied individually |
| `Galileo`, `GLONASS` | bool | No observation types are selected for either system |
| `estimator` | int | `1` least squares (implemented), `2` Kalman (not wired — see the roadmap) |

### `bias.ini`

Consumed by `system_bias` and `read_rinex`.

| Key | Type | Default | Effect |
|---|---|---|---|
| `obsFile` | path | `data/WUH200CHN_R_20250010000_01D_30S_MO.rnx` | RINEX observation file |
| `navFile` | path | `data/BRDC00IGS_R_20250010000_01D_MN.rnx` | RINEX broadcast navigation file |
| `outDir` | path | `output/bias` | Output directory; created if absent. Shared by both programs — their file names do not collide |
| `stopUTC` | ISO 8601 | `2025-01-01T01:00:30` | Stop after this epoch. Empty means run to the end of the file |

> **`stopUTC` is the one key here with a history.** The two programs used to stop
> at different epochs — `system_bias` at 01:00:30, `read_rinex` at 00:00:30 — and
> one shared key cannot carry both. The shipped value is `system_bias`'s, so
> `read_rinex` now processes 123 epochs where it used to process 3. Changing this
> to 00:00:30 would instead cut `system_bias` from 123 epochs to 3, and its nine
> output files are the point of that program. See `CHANGELOG.md`.

### `eph.ini`

Consumed by `bds_eph` and `bds_gps_diff`. The first three keys are shared; the
two groups below them belong to one program each, and each program ignores the
other's.

| Key | Type | Default | Effect |
|---|---|---|---|
| `navFile` | path | `data/BRDC00IGS_R_20250010000_01D_MN.rnx` | RINEX broadcast navigation file |
| `sp3File` | path | `data/WUM0MGXFIN_20250010000_01D_05M_ORB.SP3` | MGEX precise orbit. Not committed — see `data/README.md` |
| `outDir` | path | `output/eph` | Output directory. **`bds_eph` writes no files at all**, so it is `bds_gps_diff`-only — and `bds_eph` therefore has no `--out-dir` option |
| `targetSat` | string | `C01` | `bds_eph`: satellite to compare |
| `targetUTC` | ISO 8601 | `2025-01-01T00:05:00` | `bds_eph`: the epoch to evaluate |
| `satList` | list | `G02, G15, C01, C05, C11, C20` | `bds_gps_diff`: satellites to sweep, comma-separated on one line. **Order is preserved — it is the CSV row order** |
| `startUTC` | ISO 8601 | `2025-01-01T00:00:00` | `bds_gps_diff`: first epoch. Also names the output file, `sat_pos_vel_diff_<yyyymmdd>.csv` |
| `epochCount` | int | `2880` | `bds_gps_diff`: number of epochs |
| `interval` | double (s) | `30.0` | `bds_gps_diff`: seconds between epochs |

## Command-line overrides

Every option beats the config file for that run. The flag set is **not** the
same for every program — `bds_eph` reports one satellite at one epoch and writes
no files, so it has no `--out-dir`; `bds_gps_diff` is parameterised by an epoch
count rather than a stop epoch, so it has no `--stop`.

| Program | Config | Options |
|---|---|---|
| `spp_if` | `spp.ini` | `--obs` `--nav` `--out-dir` `--stop` `--mode` `--no-trop` `--no-bdstgd` `--gps-only` `--bds-only` `--verbose` |
| `cs_detect_mw`, `cs_detect_gf` | `cs.ini` | `--obs` `--out-dir` `--stop` `--mode` `--threshold` `--window` `--delta-t-max` `--verbose` |
| `system_bias`, `read_rinex` | `bias.ini` | `--obs` `--nav` `--out-dir` `--stop` `--verbose` |
| `bds_eph` | `eph.ini` | `--nav` `--sp3` `--sat` `--epoch` `--verbose` |
| `bds_gps_diff` | `eph.ini` | `--nav` `--sp3` `--out-dir` `--sats` `--start` `--epochs` `--interval` `--verbose` |

Every one of them also accepts `-h` / `--help`.

## Validation

`examples/parse_config` is a teaching example with a **fixed** list of keys —
the SPP ones — so it will report every key it does not know as "not present",
whichever file you hand it. It is a useful read-through of the reader's typed
accessors, not a validator for these profiles.

```bash
./build/bin/parse_config config/spp.ini
```

The regression test is the stronger check — it will catch a config change that
alters the solution:

```bash
python tests/test_regression_pipeline.py
```

And the smoke test checks the four auxiliary programs' plumbing without needing
the full dataset:

```bash
python tests/test_apps_config_smoke.py
```
