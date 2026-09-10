# Changelog

Notable changes to this project. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.0.0] — 2025

First engineering release. The numerics are unchanged from the working state
this was built on — every step below is verified against regression baselines
frozen before the work started.

### Added

- **BeiDou broadcast ephemeris** (`src/NavEphBDS.*`): orbit and clock, with the
  GEO-specific rotation that IGSO/MEO satellites do not need.
- **Doppler receiver velocity** (`src/SPPVelocity.*`): a self-contained weighted
  least-squares solution estimating `c·δṫ_r`, with robust outlier rejection
  based on a median-absolute-deviation scale rather than the post-fit residual
  sigma. The residual sigma is inflated by the outliers it is meant to find, so
  a threshold derived from it can grow with the contamination and reject
  nothing; the MAD estimate is not.
- **Configuration system**: `SPPConfigData` (a plain value type with
  `defaults()` and `fromIni()`) and non-throwing `getValueAsXOr()` accessors on
  `ConfigReader`. Two profiles, `config/spp.ini` and `config/spp.tuned.ini`.
- **Command line** for `apps/spp_if`: config file plus `--obs`, `--nav`,
  `--out-dir`, `--stop`, `--mode`, `--no-trop`, `--no-bdstgd`, `--gps-only`,
  `--bds-only`, `--verbose`.
- **Run manifest**: `apps/spp_if` writes `<obs>_manifest.json` naming the files
  it produced and the settings behind them, so the analysis layer cannot
  accidentally read a stale output.
- **`gnss_plot` Python package** and the `gnss` CLI (`build`, `spp`, `plot`,
  `run`, `demo`, `app`, `ex`). Bilingual figures and reports via `--lang en|zh`.
- **Regression test suite** in two layers: one that runs on a bare checkout and
  one that exercises the full chain, plus frozen baselines in `tests/baseline/`.
- **Sample dataset** (`data/sample/`, 2.7 MB) trimmed to the 63 epochs the
  baselines were computed from and verified to reproduce them byte-for-byte.
- **Documentation**: architecture, data formats, CLI, configuration, roadmap,
  course-chapter mapping, and per-directory READMEs.
- **CI** on Linux, Release and Debug, plus a job that builds with Ubuntu 22.04's
  own CMake so the declared minimum version is tested against a realistic floor.
- **Scripts**: `build.sh` (finds IDE-bundled CMake and Ninja), `make_sample_data.py`,
  `download_data.py`.

### Changed

- **Layout**: `lib/` → `src/`, `examples/` → `apps/` for the seven real programs,
  with the teaching examples kept in `examples/` and renamed. Program files use
  snake_case without the `exam-` prefix; the course-chapter correspondence lost
  by that rename is recorded in `docs/chapter-mapping.md`.
- **Build**: a single static `gnss` library replaces 13 per-target source lists
  that had drifted out of sync. `cmake_minimum_required` lowered from 4.1 to
  3.20 — the 4.1 floor is a hard configure error on Ubuntu 22.04/24.04 and on
  GitHub's `ubuntu-latest`. Executables now land in `build/bin/`.
- **Runtime is linked statically** on MinGW. By default MinGW links
  `libstdc++-6.dll` dynamically, and Git for Windows ships a different MinGW
  runtime in `<git>/mingw64/bin` that usually precedes the compiler on `PATH`.
  An optimised build then dies before `main()` with
  `STATUS_ENTRYPOINT_NOT_FOUND` and prints nothing, while the same code at `-O0`
  runs — which reads as a code bug. This is why the project had only ever been
  built in Debug: the old `CMakeLists.txt` hardcoded `CMAKE_BUILD_TYPE=Debug`.
- **matplotlib is configured explicitly**, not at import. The previous module
  forced the `Agg` backend and a Windows-only font on import, which broke
  notebook and interactive use and overrode `MPLBACKEND`.

### Fixed

- `SolverKalman::getSolution` compared a `Parameter::ParameterName` against a
  `const Parameter &` and did not compile. The signature now matches
  `SolverLSQ::getSolution`, which is what the body already assumed.
- `M_PI` → `PI` from `Const.h` at two sites. `M_PI` is a POSIX extension, so the
  build silently depended on `CMAKE_CXX_EXTENSIONS` being on.
- `ConfigReader::getValueAsBool` reached `stringToInt` through an `||`
  short-circuit and threw `Invalid integer value: yes` for an input it should
  have accepted.
- `ConfigData.h` declared an `extern SPPConfigData sppConfigData` that was never
  defined anywhere, so any use of it would have failed to link.
- `examples/parse_config` could never link: the target listed only
  `ConfigReader.cpp`, which has no `main()`. It now has a real program.
- The plotting code read from `D:\GnssLab\date`, a directory that does not exist;
  the real one is `data`.
- `xyz2blh` divided by `cos(lat)` for the height, which is 0/0 at the poles.
- A LaTeX label in the string table, `c\cdot\delta\dot{t}_r`, whose braces were
  read as format placeholders and would have raised `KeyError` when formatted.

### Removed

- `src/SPPUCCodePhase.*` — undifferenced PPP, unused by any program here.
- The RTK example programs (`exam-8.1` through `exam-8.5`) and the five
  coordinate-conversion examples, superseded by `examples/ecef_enu_test.cpp`.
- Third-party reference documents and the RNXCMP decompressor binaries from the
  repository. Both are still used locally; `docs/references/README.md` and
  `tools/README.md` link to them instead, as redistribution terms are unclear.
- Eigen's `bench/`, `test/`, `doc/`, `demos/`, `unsupported/` and related
  directories — 1787 files down to 353. Only `<Eigen/Dense>`, `<Eigen/Eigen>`
  and `<Eigen/Geometry>` are used, all inside `Eigen/`.

### Notes

The regression baselines were frozen **before** any of this work, and every
change above is verified against them: `apps/spp_if` still reproduces both
output files byte-for-byte, and the reference statistics in the README are
unchanged.

One user-visible behaviour change: `config/spp.ini` deliberately carries an
elevation mask of 10°, not the 15° in the placeholder ini that shipped with the
framework. The solver's own default is 10°, and the baselines were produced with
it. Wiring 15° through would have moved the solution and looked like a
regression. See `config/README.md`.
