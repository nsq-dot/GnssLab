# Command-line interface

```
gnss build     configure and build the C++ targets
gnss spp       run the SPP + velocity solver
gnss plot      analyse and plot existing solver output
gnss run       spp + plot
gnss demo      run end to end on the bundled sample, no arguments
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

## app

```
gnss app bds-eph | bds-gps-diff | read-rinex | system-bias | cs-detect-mw | matrix
```

Runs the corresponding binary from `build/bin/` with the working directory set
to the project root, so its relative paths resolve correctly. Note that several
of these still expect the full dataset, not the sample.

## ex

```
gnss ex parse_opt | parse_config | gpst_to_utc | bdweek_to_commontime | jd2020_test | ecef_enu_test
```

The interactive teaching examples. See [`../examples/README.md`](../examples/README.md).

## Exit codes

`0` success, `1` runtime failure (a missing file, a failed build), `2` a
malformed command line. The auxiliary programs propagate their own exit code.
