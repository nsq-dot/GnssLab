# Configuration reference

Configuration files use `key = value`, one per line.

Two formatting rules cause most of the confusion:

- **A line is a comment only when `#` is its first character.** An inline
  trailing comment is *not* stripped: `cutOffElevation = 15  # wider mask`
  parses as the value `15  # wider mask`, which then fails to convert. Put
  comments on their own line.
- **Every key is optional.** Anything absent keeps the built-in default, so a
  three-line file is valid. A malformed value in an optional setting also falls
  back to the default rather than aborting the run.

Relative paths resolve against the **project root** (the parent of this
directory), not the current working directory, so a config works from anywhere.
A path given on the command line instead resolves against the shell's working
directory.

## Profiles

| File | Purpose |
|---|---|
| `spp.ini` | Reproduces `tests/baseline/`. Use this unless you have a reason not to. |
| `spp.tuned.ini` | A wider elevation mask and a tighter code sigma. Deliberately does **not** reproduce the baseline. |

> **Do not change `cutOffElevation` in `spp.ini` to 15**, and do not add a
> `noiseGPSCode` override there. `SPPIFCode`'s constructor defaults to a 10°
> mask and a 1.0 m code sigma, and the baseline was produced with those. The
> observation weight is `1/sigma²`, so setting the sigma to 0.3 multiplies every
> weight by about 11×. Either change will move the solution and break the
> regression test — which will look like a code bug and is not one. Put
> experiments in `spp.tuned.ini`.

## Keys

### Consumed

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

### Reserved

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

## Command-line overrides

Any of these beats the config file for that run:

```
--obs <file>        --nav <file>       --out-dir <dir>
--stop <ISO8601>    --mode <DUAL_IF|DUAL_RAW>
--no-trop           --no-bdstgd
--gps-only          --bds-only
--verbose
```

## Validation

`examples/parse_config` prints every key it finds with its parsed type, which is
the quickest way to check a file:

```bash
./build/bin/parse_config config/spp.ini
```

The regression test is the stronger check — it will catch a config change that
alters the solution:

```bash
python tests/test_regression_pipeline.py
```
