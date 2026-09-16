# Command-line interface

```
gnss build     configure and build the C++ targets
gnss spp       run the SPP + velocity solver
gnss plot      analyse and plot existing solver output
gnss run       spp + plot
gnss demo      run end to end on the bundled sample, no arguments
gnss cs-plot   plot cycle-slip detector output (chapter 7)
gnss app       run an auxiliary program
gnss ex        run a teaching example
```

Try `gnss demo` first. It needs no downloaded data and produces a report plus
four figures.

## Invocation

The console script is registered by `pip install -e python/`:

```bash
gnss --help
```

If that is not on `PATH` — the usual reason being that the virtual environment
is not activated — the module entry point always works:

```bash
python -m gnss_plot --help
PYTHONPATH=python/src python -m gnss_plot --help     # without installing
```

In order of reliability: `python -m gnss_plot`, then `<venv>/bin/gnss`
(`<venv>\Scripts\gnss.exe` on Windows), then plain `gnss` after activation.

The C++ binaries are usable directly and do not involve Python at all:

```bash
./build/bin/spp_if --help
./build/bin/spp_if config/spp.ini --verbose
```

## build

Locates CMake and Ninja, including IDE-bundled copies that are not on `PATH`,
then configures and builds into `build/` with the executables in `build/bin/`.

```
--build-dir <dir>       default build/
--build-type <type>     Release (default), Debug, RelWithDebInfo, MinSizeRel
--generator <name>      override CMake's generator
-j, --jobs <n>          default: CPU count
```

Set `CMAKE_EXE` / `NINJA_EXE` to override the search.

## spp

```
gnss spp [config.ini] [--obs F] [--nav F] [--out-dir D]
                      [--stop ISO8601] [--mode DUAL_IF|DUAL_RAW]
                      [--no-trop] [--no-bdstgd] [--gps-only] [--bds-only]
                      [--verbose]
```

Relative paths in the config file resolve against the project root; paths given
on the command line resolve against the working directory. Options override the
config file.

## plot

```
gnss plot --rinex F [--spp-out F] [--pos-vel F] [--out-dir D]
                    [--png-dir D] [--mode M] [--station LABEL]
                    [--lang en|zh] [--no-figures] [--no-report]
```

`--rinex` is required: the reference coordinate comes from its header, and
nothing is hardcoded. The output files are located through the manifest when one
is present, falling back to the filename convention.

`--lang` defaults to `en`.

## run

`spp` followed by `plot`, sharing one set of options. `--out-dir` and `--mode`
apply to both stages.

## demo

Runs against `data/sample/` with no arguments. Writes to `output/demo/`, and
with `--docs-figures` writes the figures into `docs/figures/` instead — which is
how the images in the main README were produced.

## cs-plot

```
gnss cs-plot [--cs-dir DIR] [--run-1hz DIR] [--run-30s DIR]
             [--run-1hz-mw DIR] [--run-30s-mw DIR]
             [--inject-1hz DIR] [--inject-30s DIR]
             [--inject-1hz-mw DIR] [--inject-30s-mw DIR]
             [--manifest-1hz F] [--manifest-30s F]
             [--series DIR:SAT] [--mw-series DIR:SAT] [--threshold M]
             [--png-dir DIR] [--docs-figures] [--lang en|zh]
```

Plots the cycle-slip detectors' output: reported slip rate per satellite, one
satellite's geometry-free series with the detection markers and the threshold,
the detector state composition, and the per-case outcome of the injected slips.
It also plots the MW combination — its own rate and series, where its reports
agree with the GF detector's and where they do not, and the GF null space on the
injected run.
Reads existing detector output — it does not run the detectors; use
`gnss app cs-detect-gf` for that. See
[cycle-slip-gf.md](cycle-slip-gf.md) for the analysis and the commands that
produce the input.

The run directories default to the layout that document's reproduction section
prescribes, under `--cs-dir` (default `output/cs`):

| Option | Default |
|---|---|
| `--run-1hz` | `<cs-dir>/zero-1hz` |
| `--run-30s` | `<cs-dir>/sample-30s` |
| `--inject-1hz` | `<cs-dir>/injected/run` |
| `--inject-30s` | `<cs-dir>/injected/run-30s` |
| `--manifest-1hz` | `<cs-dir>/injected/oem719-injected.obs.slips.csv` |
| `--manifest-30s` | `<cs-dir>/injected/wuh2-injected.rnx.slips.csv` |

Those names are not produced by the C++ programs — `--out-dir` writes whatever
it is told — so each one can be overridden.

`--series DIR:SAT` draws one satellite's series and may be repeated; `DIR` is
relative to `--cs-dir`. The defaults are `injected/run:G01` and
`sample-30s:G24`, chosen to show the two failure modes the chapter is about.

§一's figure — G08's MW combination on `--run-1hz-mw`, with the epochs the
detector flagged — has no option of its own. It is always the first figure drawn,
and it is skipped along with everything else when that run is absent.

**A missing run is skipped with a `warning:` on stderr, not a failure**; the
command fails only if it produced no figure at all. That matters because the
1 Hz zero baseline is not committed, so on a fresh clone only half of the
experiment exists:

```bash
# what a fresh clone can do
PYTHONPATH=python/src python -m gnss_plot.cli cs-plot --lang zh --series sample-30s:G24
```

`--threshold` defaults to the value recorded in the run's own `summary` metadata.

## app

```
gnss app bds-eph | bds-gps-diff | read-rinex | system-bias | cs-detect-mw | cs-detect-gf | matrix
```

Runs the corresponding binary from `build/bin/` with the working directory set
to the project root, so its relative paths resolve correctly.

All of these except `matrix` take `[config.ini] [options]` rather than running
bare, because they read their input paths from a config file:

| Program | Config | Options |
|---|---|---|
| `bds-eph` | `config/eph.ini` | `--nav` `--sp3` `--sat` `--epoch` `--verbose` |
| `bds-gps-diff` | `config/eph.ini` | `--nav` `--sp3` `--out-dir` `--sats` `--start` `--epochs` `--interval` `--verbose` |
| `read-rinex` | `config/bias.ini` | `--obs` `--nav` `--out-dir` `--stop` `--verbose` |
| `system-bias` | `config/bias.ini` | `--obs` `--nav` `--out-dir` `--stop` `--verbose` |
| `cs-detect-mw`, `cs-detect-gf` | `config/cs.ini` | `--obs` `--out-dir` `--stop` `--mode` `--threshold` `--window` `--delta-t-max` `--verbose` |

Everything after the program name is passed through verbatim, so the `--` is
optional: `gnss app cs-detect-gf --mode both --obs data/sample/...` works, and
so does the `--` spelling.

The shipped defaults point at the **full** dataset, which is not committed. What
each program needs from it differs:

- `read-rinex` and `system-bias` run on the committed sample — override the two
  input paths, e.g.
  `gnss app read-rinex --obs data/sample/WUH200CHN_R_20250010000_01D_30S_MO.rnx --nav data/sample/BRDC00IGS_R_20250010000_01D_MN.rnx`
- `bds-eph` and `bds-gps-diff` additionally need the SP3 precise orbit, and
  **`data/sample/` does not contain one** — see
  [data/README.md](../data/README.md) for where to get it.

See [config/README.md](../config/README.md) for the key reference, and
[cycle-slip-gf.md](cycle-slip-gf.md) for the cycle-slip programs.

## ex

```
gnss ex parse_opt | parse_config | gpst_to_utc | bdweek_to_commontime | jd2020_test | ecef_enu_test
```

The interactive teaching examples. See [`../examples/README.md`](../examples/README.md).

## Exit codes

`0` success, `1` runtime failure (a missing file, a failed build), `2` a
malformed command line. The auxiliary programs propagate their own exit code.
