# Architecture

## Shape of the system

```
   RINEX obs/nav ─┐
   SP3 orbit      ├─►  C++20 engine (src/, apps/)  ─►  *.out + manifest.json  ─►  Python layer (python/)  ─►  report + PNG
   CLK clock      ┘         numerics                    the contract                   analysis
```

Two languages, one seam. The C++ side does every numerical operation and writes
plain text; the Python side reads that text and produces statistics and figures.
Neither imports the other, and neither needs to be rebuilt when the other
changes.

This is a deliberate choice, not an accident of history. An alternative design
would put the plotting in C++ (via a plotting library) or the estimation in
Python (via numpy), but each would couple the parts: the numerics would need a
rebuild to change a chart, and the charts would need a Python environment to
change an estimator setting. A text contract lets each side be replaced
independently — and it means the solver output can be inspected, diffed, and
archived with ordinary tools.

The cost is that the contract has to be specified and tested. It is documented
in [data-format.md](data-format.md) and enforced by `tests/test_regression_pipeline.py`.

## Why the CLI is in Python

`gnss` is a Python console script that orchestrates the C++ binaries. A C++
dispatcher was considered and rejected:

- It could not run the plotting step. The seam is a *file*, not a link, so
  driving matplotlib would mean embedding a Python interpreter.
- It would still need the same "find my sibling binaries" logic.
- It would duplicate argument handling that `argparse` provides.
- Most importantly, `gnss --help` would require a compiler and a build before it
  printed anything, which is a poor first thirty seconds for anyone evaluating
  the project.

The C++ executables remain fully usable on their own — `apps/spp_if --help`
works and does not involve Python at all. The CLI is a convenience layer above
them, not a wrapper they depend on.

## The C++ engine

A single static library, `gnss`, built from `src/`, with thin programs in `apps/`
and `examples/` linking against it.

| Group | Modules |
|---|---|
| Foundation | `Const.h`, `Exception.h`, `StringUtils.h`, `MathUtils.hpp` |
| Time | `TimeStruct.*`, `TimeConvert.*` |
| Coordinates | `CoordStruct.h`, `CoordConvert.h` |
| Core data | `GnssStruct.*` — `SatID`, `ObsID`, observational data |
| Algorithms | `GnssFunc.*` — satellite position/clock, error models |
| Product readers | `RinexObsReader.*`, `RinexNavStore.*`, `NavEphGPS.*`, `NavEphBDS.*`, `SP3Store.*`, `Rx3ClockReader.*`, `EphStore.h` |
| Estimation | `SolverLSQ.*`, `SPPIFCode.*`, `SPPVelocity.*`, `CSDetector.*` |
| Not yet wired | `SolverKalman.*`, `KalmanFilter.*`, `ARLambda.*` |
| Configuration | `ConfigData.*`, `ConfigReader.*` |

`gnss` is a **static** archive. It emits no shared library, so there is no DLL
to ship, no import library, and no runtime search path — which is why the
originally duplicated per-target source lists could be collapsed. See the
comment in `CMakeLists.txt` about static linking of the GCC runtime for the
related MinGW issue.

## The Python layer

`python/src/gnss_plot/`, installed as the `gnss-plot` package.

| Module | Responsibility |
|---|---|
| `io.py` | Reads the solver output and the RINEX header; reads the manifest |
| `coords.py` | ECEF ↔ geodetic, ECEF deltas → ENU |
| `stats.py` | bias / sigma / RMS, and the console report |
| `figures.py` | The four `fig_*` functions and matplotlib setup |
| `_strings.py` | Every user-visible string, in English and Chinese |
| `_find.py` | Locates CMake, Ninja, and the compiled binaries |
| `cli.py` | Subcommand dispatch |

Nothing is configured at import: matplotlib's backend is chosen in
`figures.configure()`, not on import, so the package remains usable from a
notebook or an interactive session.

## Data flow in a run

1. `gnss spp` runs `apps/spp_if`, which reads the config file, resolves paths
   against the project root, and loads the navigation file into an ephemeris
   store.
2. For each epoch it parses the observation record, solves position and clock
   by weighted least squares, and estimates velocity from the Doppler
   observations with robust outlier rejection.
3. It writes `<obs>_<mode>.spp.out`, `<obs>_pos_vel.out`, and
   `<obs>_manifest.json`.
4. `gnss plot` reads the manifest to locate the two outputs, parses the RINEX
   header for the reference coordinate, and produces the report and figures.

## Extension points

**A new figure.** Add a `fig_*` function to `figures.py` following the existing
contract (arrays in, path out, path returned) and register it in `cli.py`.

**A new auxiliary program.** Add the source to `apps/`, list it in `GNSS_APPS`
in `CMakeLists.txt`, and add a name to `AUX_APPS` in `cli.py` — after which
`gnss app <name>` runs it.

**A new configuration key.** Add the field to `SPPConfigData`, read it in
`fromIni()` with the `...Or()` accessor so it stays optional, and document it in
`config/README.md`. If nothing consumes it yet, put it under the reserved banner
in `config/spp.ini` rather than implying it works.
