# GnssLab

**A GNSS data-processing system: a C++20 engine for single-point positioning and
Doppler velocity, with a Python layer for accuracy analysis and figures.**

Reads RINEX observation and broadcast navigation files, solves position and
receiver clock epoch by epoch, estimates receiver velocity from the Doppler
observations, and reports accuracy with reproducible figures.

[中文文档](README.zh-CN.md) · [Architecture](docs/architecture.md) · [Data formats](docs/data-format.md) · [CLI](docs/cli.md) · [Roadmap](docs/roadmap.md)

![Position error time series](docs/figures/vis_pos_error_enu_ts.png)

<p align="center">
  <img src="docs/figures/vis_pos_horizontal.png" width="45%" alt="Horizontal error distribution">
  <img src="docs/figures/vis_clock_drift.png" width="45%" alt="Receiver clock drift">
</p>

---

## Results

Station **WUH2** (Wuhan), 2025-01-01, GPS + BeiDou, 30 s sampling, 63 epochs
(00:00:00–00:31:00 UTC). The station is static, so the velocity truth is exactly
zero.

**Velocity** — metre-per-second level, from the Doppler observations:

| Component | bias | sigma (internal) | RMS |
|---|---|---|---|
| vE (east) | −0.008061 m/s | 0.005828 | 0.009920 |
| vN (north) | +0.001240 m/s | 0.005055 | 0.005165 |
| vU (up) | −0.002883 m/s | 0.017616 | 0.017712 |
| horizontal 2D | | | 0.01118 m/s |
| 3D | | | 0.02095 m/s |

Receiver clock drift 1.2496–1.9828 m/s, mean 1.6083 m/s — about
5.4 × 10⁻⁹ s/s, an ordinary crystal oscillator rate.

**Position** — relative to the RINEX header coordinate:

| Component | bias | sigma (internal) | RMS |
|---|---|---|---|
| E | +2.197 m | 1.165 m | 2.483 m |
| N | +3.979 m | 1.050 m | 4.113 m |
| U | +11.419 m | 2.992 m | 11.799 m |

### On reading these numbers

**sigma** is the standard deviation about the series mean (`ddof=1`). It measures
*precision* — repeatability — and is unaffected by a constant offset.

**RMS** is measured against a reference and therefore includes any systematic
offset, so it is always ≥ sigma.

For velocity the reference is exact (a static station does not move), so those
numbers are a genuine accuracy statement.

For position the reference is the RINEX header `APPROX POSITION XYZ`, which is
itself only **metre-to-decametre accurate**. The mean solution differs from it by
(−5.22, +6.21, +9.23) m, so the position RMS above combines repeatability with a
reference-coordinate offset and is **not** absolute accuracy. The internal sigma
is unaffected and is the meaningful precision figure here. A rigorous accuracy
assessment needs an IGS `.snx` truth coordinate; see the roadmap.

The vertical sigma being ~3× the horizontal is expected for single-point
positioning and reflects satellite geometry.

## Quick start

Requirements: a C++20 compiler (GCC 11+, Clang 14+, or MSVC 19.3+), CMake 3.20+,
and Python 3.9+ with numpy and matplotlib. Ninja is used when available.

```bash
git clone <your-fork-url> && cd GnssLab

cd python && pip install -e . && cd ..   # registers the `gnss` command
gnss build                               # configure and build all 13 targets
gnss demo                                # run and plot the bundled sample
```

`gnss build` locates CMake and Ninja itself, including IDE-bundled copies that
are not on `PATH`. If it cannot find them, set `CMAKE_EXE` or use
`scripts/build.sh`, which does the same search.

`gnss demo` needs no arguments and no downloaded data — it runs the committed
2.7 MB sample and writes a report plus four figures to `output/demo/`.

Without installing the Python package:

```bash
scripts/build.sh
PYTHONPATH=python/src python -m gnss_plot.cli demo
```

## Using it on your own data

```bash
# 1. Put your RINEX files in data/ and point the config at them.
$EDITOR config/spp.ini

# 2. Run the solver.
gnss spp config/spp.ini

# 3. Analyse and plot.
gnss plot --rinex data/YOUR_OBS.rnx --out-dir output
```

Or in one step: `gnss run config/spp.ini`.

Options override the config file:

```bash
gnss spp config/spp.ini --stop 2025-01-01T12:00:00 --gps-only --verbose
```

See [docs/cli.md](docs/cli.md) and [`config/README.md`](config/README.md).

## What it does

**Implemented**

- Single-point positioning by weighted least squares on the dual-frequency
  ionosphere-free code combination (GPS L1/L2, BeiDou B1I/B3I)
- Doppler-based receiver velocity, in a separate weighted least-squares step,
  with robust outlier rejection based on a median-absolute-deviation scale
- Broadcast ephemeris: GPS and BeiDou, including the GEO-specific orbit rotation
- Elevation mask, Hopfield / Saastamoinen tropospheric delay, BeiDou TGD
- Melbourne–Wübbena cycle-slip *detection*
- Systematic-error diagnostics: TGD, ionospheric and tropospheric delay
- Broadcast-versus-precise orbit and clock comparison against SP3/CLK
- Configuration files, a command line, and a unified `gnss` entry point

**Not implemented** — stated so you do not have to infer it from the source

- **No RTK and no PPP.** Carrier-phase relative positioning is absent. The
  chapter-8 material (`ARLambda`, `SolverKalman`, `KalmanFilter`) is in the
  library and compiles, but no program wires it up.
- **No Kalman filter.** `estimator = 2` parses but is ignored; the solver always
  uses least squares.
- **No Galileo or GLONASS.**
- **No cycle-slip repair**, only detection.

[`docs/roadmap.md`](docs/roadmap.md) has the full list, including known issues
and suggested next steps.

## How it fits together

```
  RINEX obs/nav ─┐
  SP3 orbit      ├─►  C++20 engine  ─►  *.out + manifest.json  ─►  Python layer  ─►  report + PNG
  CLK clock      ┘     numerics            the contract              analysis
```

The C++ side does every numerical operation and writes plain text; the Python
side reads that text. Neither imports the other, so the solver output can be
inspected, diffed and archived with ordinary tools, and neither side needs
rebuilding when the other changes.

The `gnss` CLI is Python because it has to drive both halves — the interface
between them is a file, not a link. The C++ executables in `build/bin/` are
fully usable on their own.

[docs/architecture.md](docs/architecture.md) explains the split and the
alternatives that were rejected.

## Repository layout

| Directory | Contents |
|---|---|
| `src/` | The GNSS library — 18 translation units, built as a static `gnss` archive |
| `apps/` | The seven real programs, including `spp_if` |
| `examples/` | Six interactive teaching programs, no data needed |
| `python/` | The `gnss_plot` package and the `gnss` CLI |
| `config/` | Solver configuration profiles |
| `data/` | Data — only a 2.7 MB sample is committed |
| `tests/` | Three test layers and the frozen regression baseline |
| `docs/` | Architecture, formats, CLI, configuration, roadmap |
| `scripts/` | Build, sample generation, data download |
| `thirdparty/` | Eigen 3.4.0, headers only |

## Testing

```bash
python tests/test_plot_utils.py             # no data, no build needed
python tests/test_baseline_numerics.py      # no data needed
python tests/test_regression_pipeline.py --required
```

The third runs the built solver on the sample and requires **byte-identical**
output against `tests/baseline/` — the strongest available statement that a
change altered nothing numerically. The first two run on a bare checkout, so a
contributor who has not downloaded 500 MB of data still gets a meaningful
result. See [`tests/README.md`](tests/README.md).

## Data

`spp_if` needs a RINEX observation file and a broadcast navigation file. Neither
is committed; only a trimmed sample is. Fetch the real products with:

```bash
python scripts/download_data.py --product all --date 2025-01-01
```

or by hand from any IGS data centre — see [`data/README.md`](data/README.md) for
the products, the directory layout, and the station details.

## Background

Built on the **gnssLab-2.4** teaching framework by Shoujian Zhang (School of
Geodesy and Geomatics, Wuhan University), which supplies the base library and the
course-chapter example structure. The BeiDou ephemeris handling, the Doppler
velocity solution, the configuration system, the analysis and plotting layer, and
the restructure are additions to it.

Work that came from the framework keeps its original attribution; the additions
are listed in [`NOTICE`](NOTICE), which MulanPSL-2.0 requires for modified
versions. [`docs/chapter-mapping.md`](docs/chapter-mapping.md) maps each program
to its original course chapter.

## License

[MulanPSL-2.0](LICENSE) — a permissive license from the OpenAtom Foundation,
matching the headers already present in the upstream sources.

`thirdparty/eigen-3.4.0` is **not** covered by that grant: Eigen is licensed
primarily under MPL-2.0, with some files under BSD, LGPL and Apache. See
[`NOTICE`](NOTICE) §3 and `thirdparty/eigen-3.4.0/COPYING.README`.

## Citing

```bibtex
@misc{gnsslab2025,
  title  = {GnssLab: GNSS single-point positioning and Doppler velocity estimation},
  author = {Wu, Donghao},
  year   = {2025},
  note   = {Built on the gnssLab-2.4 framework by Shoujian Zhang, Wuhan University},
  url    = {https://github.com/<your-username>/GnssLab}
}
```
