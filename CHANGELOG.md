# Changelog

Notable changes to this project. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **Figure 7-11, the mirror of the null-space figure, and an MW row in figure 7-4.**
  The chapter's central claim — that the two combinations have *complementary*
  blind spots — was carried by one figure showing only the half where GF is
  blind, so "MW is blind to `dN1 == dN2`" stayed a table entry with nothing drawn
  behind it. Figure 7-11 injects `(1, 1)` on G01 and shows the reverse: the GF
  statistic jumps to 0.056 m, 1.9x over its 0.030 m threshold, while the MW
  combination moves by 0.006 cycles. Both figures come out of the same
  `fig_nullspace`, which gained a `title_key`; the case is chosen by manifest
  label, so re-running the injector moves them with it. Figure 7-4 gained a third
  row for MW — the GF detectors and MW miss disjoint cases, and a column of
  answers that stopped at two detectors showed only half the misses.
- **`system_bias`, `read_rinex`, `bds_eph` and `bds_gps_diff` are configurable.**
  Two new profiles, `config/bias.ini` (shared by the first two) and
  `config/eph.ini` (shared by the last two), backed by `BiasConfigData` and
  `EphConfigData` in `src/ConfigData.*`. Each program gained a command line in
  the same shape as `spp_if`'s: a positional config file, options that override
  it, and `-h`. `bds_gps_diff` also gained `--sats` / `--start` / `--epochs` /
  `--interval` for the series it sweeps, and `bds_eph` `--sat` / `--epoch` for
  the single comparison — all four were previously constants in the source.
- `findConfigUpwards()` and `splitList()` in `src/ConfigData.*`, the latter
  because the reader knows only scalar values and a satellite list has to be
  written as one comma-separated line.
- `tests/test_apps_config_smoke.py`, which checks the above without needing the
  full dataset.
- **Five figures for chapter 7, and the `gnss cs-plot` command that draws them.**
  `docs/cycle-slip-gf.md` had ten tables and no figures; it now carries the
  per-satellite slip-rate comparison, one satellite's geometry-free series
  against the detection threshold, the detector state composition, and the
  per-case outcome of the injected slips.
- `gnss_plot.cycleslip`, and three readers in `gnss_plot.io`
  (`load_gf_summary`, `load_gf_detector`, `load_slip_manifest`).
  `scripts/check_cycle_slips.py` now scores through `cycleslip` instead of
  carrying its own copy of the join, so the figure and the printed scoreboard
  cannot disagree. Its output is unchanged, verified byte for byte.
- `scripts/inject_cycle_slips.py` now creates its output directory. The
  documented workflow writes into `output/cs/injected/`, which does not exist on
  a fresh clone, and the failure was a bare `FileNotFoundError` from inside the
  writer.
- **The MW combination's own results, scored and plotted.** `docs/cycle-slip-gf.md`
  had one paragraph about MW and no figures of it; it now carries MW's own
  detection rates and the cross-validation, four figures (7-7 to 7-10), and the
  run commands those were missing. What it establishes
  is that the two combinations have *different* null spaces — GF is blind to
  (77k, 60k) because 77·λ1 = 60·λ2, MW is blind to (k, k) because its statistic
  is N1 − N2 and equal slips cancel in it — so neither is complete and only the
  union covers both. On the injected 1 Hz run MW detects the (77, 60) case that
  the GF detector provably cannot, and misses every equal pair the GF detector
  catches.
- `check_cycle_slips.py --mode mw`, `cycleslip.compare_flagged()`, and the `mw`
  entries in `FILENAME_SUFFIX` and `load_detector_run()`. MW writes a 0/1 `flag`
  where the GF detectors write a state token, so the translation to OK/SLIP is
  deliberately blunt: that flag does not separate an arc start or a post-gap
  epoch from a real slip, and the adversarial ground-truth rows are what expose
  it.
- `figures.fig_mw_series`, `figures.fig_crosscheck` and `figures.fig_nullspace`,
  plus a `detectors` parameter on `fig_slip_rate_bars` (its two-series default
  renders figure 7-5 unchanged, verified byte for byte) and a `--mw-series`
  option on `gnss cs-plot`, which now draws eleven figures rather than five.
- **A figure for §一 of chapter 7: one satellite's MW combination across a whole
  1 Hz pass, with the epochs the detector flagged marked on it.** G08, out of the
  26 satellites the 1 Hz run covers, and picked by reading them rather than by
  counting flags: on this dataset the combination is flat wherever it tracks well,
  so what separates the candidates is where the flags fall. Most satellites flag
  nothing but their arc start (G01, G07, G21: 2 flags over ~7900 epochs), which
  draws a clean line with nothing to see; the noisy ones (G14, C03) have a
  combination that itself wanders by tens of cycles and flag in blocks, which
  reads as a smear rather than as slips. G08 sits between — 95% of its 6544 values
  span 0.33 cycles (p95 − p5) and its 25 flags are spread along the hour. `fig_mw_series`
  gained a `deviation` switch for it, which drops the detector-statistic panel
  that figure 7-8 keeps.
  **Figures 7-1 to 7-10 replace the old 7-1 to 7-9 numbering**: this figure
  belongs to §一, so every later figure moved up by one, and the references in
  `figures.py`, `cli.py` and this file moved with them.

### Changed

- **The MW figures plot cycles, not metres.** `cycleslip.series_view` divides the
  MW combination — and its deviation from the recursive mean — by the wide-lane
  wavelength, so `li` is now the wide-lane ambiguity `N_W = N1 − N2` and the new
  `cycleslip.mw_wavelength(sat)` supplies 0.8619 m for GPS and 0.8470 m for
  BeiDou B1I/B2b, mirroring `getFreq()` in `src/Const.h`. Figures 7-1, 7-8 (both
  panels) and the lower panel of 7-10 are affected; figure 7-10's upper panel is
  GF and keeps metres. The chapter's tables, its injected `(dN1, dN2)` pairs and
  its blind-spot results are all in cycles, so the figures now read directly
  against them — G08's largest step goes from "69 m" to "79 cycles". The GF
  figures are deliberately left in metres: `L_I` is not an integer multiple of
  any wavelength and its ionospheric term is natively metres, so labelling it
  "cycles" would imply a periodicity it does not have. No detector output and no
  baseline changed — this is a plotting-layer conversion only.
- **Those four programs write to `output/bias/` and `output/eph/` instead of the
  working directory.** The `gnss app` CLI runs them with the repository root as
  the working directory, so their fixed-name CSV and text files used to land in
  the checkout.
- **`read_rinex` now stops after 123 epochs where it stopped after 3.** Its stop
  epoch was 00:00:30, a debugging leftover — its own commented-out alternative
  was `23:59:30`, the whole day. It now shares `stopUTC` with `system_bias`,
  which has always stopped at 01:00:30, and one shared key cannot carry two
  values. `system_bias` is unchanged at 123 epochs; its nine output files are
  byte-identical to before. Pass `--stop 2025-01-01T00:00:30` to reproduce the
  old `read_rinex` output exactly.
- `bds_gps_diff` names its CSV from the configured start date
  (`sat_pos_vel_diff_<yyyymmdd>.csv`) rather than the hardcoded `20250101`. The
  default start still produces the old name.
- `data/README.md` no longer claims `bds_gps_diff` reads the `.CLK` file. It does
  not; no executable loads one.
- **`docs/cycle-slip-gf.md` is organised by detector rather than by artefact.**
  The MW combination used to be a standalone closing chapter, which read as a
  separate topic; it is the third detector, so its principle now sits beside
  GF's in §二, its detection rates beside the other two in §六, and only the
  cross-validation and the complementary-blind-spots conclusion remain in §七.
  Figures 7-1 and 7-2 moved into the two detector sections (exercises 1 and 3),
  which previously carried no figure at all, and 7-3 into the injection section,
  so each exercise now reads text-and-figure together instead of a block of
  prose followed by a block of images. **Figure numbers are unchanged** — the
  old order already matched the new placement, so nothing outside this file
  needed updating.

### Removed

- The absolute paths compiled into those four programs (`D:\GnssLab\data\...`),
  which made them work on exactly one machine.
- `#define debug 1` and the unconditional per-epoch and per-satellite stdout
  dumps in `system_bias` and `read_rinex`; the detail is now behind `--verbose`,
  and a run prints a short summary instead. Every number that was dropped from
  the default output also goes to one of the output files.
- `bds_eph`'s three hardcoded clock strings (`elaptc:286`, and the `af0:`/`dtc:`
  lines beside it). They were frozen copies of what the clock model prints
  itself, so they read as results while nothing computed them.
- `read_rinex`'s unused `SPPIFCode.h` include, its `solFile` variable that was
  computed and never written, and the second navigation load that existed only to
  dump the whole ephemeris store to stdout.
- `bds_gps_diff`'s file-scope `satList` global, now built in `main` from the
  config and validated before use.
- **`src/CSDetector.*`, the class-based cycle-slip detector.** Nothing ever
  called it: `apps/cs_detect_mw.cpp` uses the free function `detectCSMW()`, and
  `CSDetector::detect()` had no caller anywhere in the repository — or in the
  upstream course material it arrived from unchanged, where it was equally
  unreferenced. It implemented the MW combination rather than GF, so it was a
  second arrangement of logic `src/GnssFunc.cpp` already carries, and its BeiDou
  branch was an unfinished `// TODO`. Chapter 7 is unaffected — it lives in
  `detectCSGFdiff()`, `detectCSGFpoly()` and `detectCSMW()`.

### Fixed

- **Figure 7-8's lower panel plotted a quantity that is identically zero where it
  matters, and drew none of its markers.** `apps/cs_detect_mw.cpp` writes
  `meanMW_m` *after* updating it, and a detected slip resets that mean to the
  combination just read — so at every flagged epoch the stored mean equals `mw_m`,
  the difference `|mw - meanMW_m|` was exactly zero, and a zero cannot be drawn on
  a log axis, so the red triangles silently vanished. The bias the detector
  actually tests is against the mean carried *in*, which is the previous epoch's
  stored value; `cycleslip.series_view` now differences against that. The
  reconstruction reproduces every decision the detector made on the chapter's runs
  — no flagged epoch left unexplained, no unflagged epoch above either threshold —
  and figure 7-8's lower panel now shows a real curve with its markers on it. Only
  that one figure changed. The remaining gap (the adaptive `4*sqrt(varMW)`
  threshold cannot be drawn, and `csFlagArg` carries no information) is in
  `docs/roadmap.md`.
- **`vis_cs_gf_mw_crosscheck.png` came out different on every regeneration.**
  `cycleslip.compare_flagged` returns sets — that is its documented shape — and
  the CLI iterated them straight into the figure. Python randomises string hashes
  per process, so the markers were drawn in a different order each run, and
  overlapping markers composite differently, which changes the PNG byte for byte.
  The three lists are sorted on the way in. Found by regenerating the figures
  twice and comparing SHA-256, which is the only check that catches this; the
  committed PNG therefore changes once, to the same points in a fixed order.
- **`spp_if` (and the cycle-slip detectors) could not run from the build tree.**
  The default config name `config/spp.ini` was looked up relative to the working
  directory only. CLion runs a target with the build tree as its working
  directory (`cmake-build-debug/bin`, two levels below the repository root), so
  the lookup missed, the program fell back to built-in defaults, and those
  defaults are relative to the working directory — the run died with
  `cannot open observation file` from a perfectly good build. The default name
  is now searched for in the parent directories (`findConfigUpwards()` in
  `src/ConfigData.*`), and the fallback message names the directory the relative
  paths will resolve against. A config named explicitly on the command line is
  still taken at its word, so a typo fails loudly instead of quietly resolving
  to a different file. Numerics unchanged: both regression baselines still
  reproduce byte-for-byte.
- **Four wrong numbers in `docs/cycle-slip-gf.md`, found while building the
  figures.** Two of them mattered: the 1 Hz and 30 s tables both carried, as the
  "judged epochs" total for the polynomial detector, the figure from the
  **injected** run rather than the clean one they were tabulating (115348
  instead of 115390, and 895 instead of 909 — the diff rows happened to agree,
  which is why it went unnoticed). The 1 Hz table also had no `INIT` column and
  parked that detector's 25 arc-start epochs under `WARMUP`, a state the
  epoch-difference detector does not have. The C01 residual percentiles were
  stale from a pre-fix run and did not reproduce at all. Each table now also
  states the identity `judged = OK + SLIP = nTested`, so a future drift is
  visible rather than silent.
- **The end-to-end regression test could not pass on Linux.** It compared the
  solver's output against the baseline byte for byte, and the baseline is a
  Windows-generated file stored as CRLF (`.gitattributes` pins `*.out` with
  `-text` so that git never rewrites it). The solver writes in text mode, so the
  C runtime expands `\n` to `\r\n` on Windows and leaves it alone on Linux — the
  same numbers, a different terminator, and a comparison that could only ever
  pass on the platform the baseline was frozen on. The comparison now normalises
  CRLF to LF and holds everything else to the byte, so the numbers are still
  checked to the same standard on both platforms. Neither the solver nor the
  baseline files changed; the hashes `test_baseline_numerics.py` asserts still
  hold.

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
