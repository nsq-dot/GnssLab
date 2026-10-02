# Changelog

Notable changes to this project. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **`apps/rtk_float` — chapter 8's RTK single-epoch least-squares float solution.**
  Both receivers are linearized at their own approximate position by
  `SPPUCCodePhase`, differenced between stations and then between satellites, and
  solved per epoch by `SolverLSQ`. Nothing is fixed to an integer and nothing is
  carried across epochs, which is what "float, single epoch" means — and, as
  [rtk.md](docs/rtk.md) §3 shows, it means the carrier phase contributes *no*
  information about position: each phase equation carries its own free ambiguity,
  so the ambiguities can absorb any position exactly, and removing every phase
  equation leaves the output bit-identical. Accuracy is therefore that of the
  pseudorange double differences, which is the honest answer to exercise 1 and
  the reason the textbook goes on to Kalman filtering and LAMBDA.
  Three constellation/frequency modes (`--sys gps|bds2|bds3`), a config file, a
  `--dump-epoch` for inspecting one epoch's system, a per-epoch diagnostics CSV,
  and a manifest that counts skipped epochs by reason.
- **`docs/rtk.md`**, carrying exercise 1: the two readers bugs it uncovered, the
  proof that the float phase carries no position information, the measured
  GPS / BDS-2 / BDS-3 comparison over 7934 epochs, and why the two BeiDou
  generations cannot share a frequency pair.
- **`config/rtk.ini` and `RTKConfigData`**, the only profile that takes two
  observation files and the only one whose `sys` key changes which satellites
  exist at all.
- **`examples/sync_obs` and `examples/diff_station`**, written from two files that
  were copyright headers and nothing else — no code, no `main()`.
  `sync_obs` drives the epoch-alignment rule (factored out as `src/EpochAlign.h`)
  through a match, a skipped epoch and an exhausted stream. `diff_station` drives
  `differenceStation` / `differenceSat` on hand-built equation systems and prints
  what they actually do — that `Parameter::iono` is deleted rather than
  differenced, that the receiver clock cancels, that the ambiguity coefficient is
  carried through unchanged while the unknown it multiplies is a difference, and
  that rank deficiency yields a plausible wrong answer rather than an error.
- **`apps/rtk_float --fix`** — the chapter-8 ambiguity resolution, on the float
  solution `rtk_float` already produced. MLAMBDA on the ambiguity block of each
  epoch's double-difference system, then the coordinate correction of the
  textbook's (8.63). This is what makes the carrier phase pay off: in the float
  solution each phase equation carries its own free ambiguity, so the
  ambiguities absorb the phase residual exactly and the phase constrains nothing;
  constraining them to integers is what lets it constrain the geometry.
  `--ratio <x>` sets the (8.61) threshold, default 3.0, and implies `--fix`.
  Output goes to a **separate** `<rover>_<sys>_rtk_fixed.out` in the textbook
  main program's layout (`spp:` / `float-rtk:` / `ratio:` / `fixed-rtk:`) rather
  than extra columns on the float file — that file is pinned byte-for-byte to the
  original program's output, which is the chapter's only external anchor. The
  diagnostics CSV gains `nAmb`, `ratio`, `fixed` and `absDxyzFixed`; the manifest
  gains the fixed-file name, the threshold and the fixed-epoch count.
  Off by default, so a default run is unchanged.
  Measured on the OEM719 zero baseline (7934 epochs, truth known exactly because
  both receivers share one antenna): 100% of epochs fixed for GPS and BDS-3,
  82.1% for BDS-2; **no accepted fix is wrong in any of the three** (worst error
  34 mm); fixed accuracy 1.1–5.1 mm against 0.076–1.128 m float. The ratio test is
  what earns that — on BDS-2, discarding it would leave 1322 epochs, 16.7% of the
  run, sitting on a metre-to-25-metre error.
- **Figures 8-4 and 8-5, and the chapter-8 reader behind them.** `gnss rtk-plot`
  gained `load_rtk_fixed`, `fix_summary` and two plots drawn only when the runs
  were fixed: the float and fixed 3-D error on figure 8-1's own window (so the
  two figures are read against each other, and the gap between the bands *is*
  the result), and the ratio distribution with the threshold marked. The fixed
  series is drawn only where the ratio test accepted — interpolating it across a
  rejection would show a series no consumer ever gets. The console table gained
  a fixing block (fixed rate, float and fixed RMS, worst accepted error, gain),
  and `rtk.md` §八 now carries exercise 2 and the three conclusions.
- **A merged BeiDou solution and the inter-system bias behind it** (`--sys bds23`,
  `--isb`). Chapter 8's exercise 2 says the two BeiDou generations differ in
  receiver clock, so they must be solved separately; the code could not merge
  them at all, because the two generations broadcast *different* second
  frequencies (B2I vs B2a) and `dualCodeTypes` insisted on one pair per system.
  That is now a *list* of pairs tried in order, so each satellite takes the first
  it can satisfy — which is what lets `bds23` see eleven satellites where the
  single-generation modes see seven and eight. The existing three modes still list one pair
  each, so their output is unchanged, anchors included.
  `--isb` adds one unknown (`Parameter::ifb`, which had sat unused in the enum):
  coefficient 1 on a BDS-3 satellite's pseudoranges, 0 on a BDS-2 one, so it
  cancels within a generation and survives across one. `SatID`'s long-standing
  `// int generation;` todo is answered by `bdsGeneration()` — a function rather
  than the field, because a member has to be set by every constructor and a
  stale one is worse than a derived one.
  Two things only the dumps revealed, both now commented in the source: the
  parameter must go on the **pseudoranges only** (on the carrier phase it is not
  separable from the ambiguity, and estimating it there drops the median
  ambiguity ratio from 1453 to 6.5 and the fixed rate from 96.7% to 91.6%), and
  `differenceSat` must take the **union** of the two coefficient maps, because
  this is the one parameter that can be present on only one side.
  Measured on data/Zero-baseline over 7934 epochs: the relative bias is
  **−12 mm with 19 mm of per-epoch scatter** — zero within the noise. So merging
  the generations costs nothing here, and the real reason they are solved
  separately is the frequency incompatibility, not the bias. Adding the
  parameter changes the fixed solution not at all (1.0 mm either way) and the
  float RMS from 0.0715 m to 0.0784 m — the cost of estimating a parameter that
  is zero.
- **`examples/exam-8.3-lambda.cpp`, built as `mlambda`** — chapter 8.3.5's
  ambiguity fixing. It was the one chapter-8 exercise source left unbuilt (and
  the only one kept under its upstream file name, so it needed an explicit target
  rather than the `examples/` loop). It resolves three hand-picked float
  ambiguity vectors: the lecture notes' own example 8-1, which fixes to
  `[-9 21 -2 4 24 7]` with ratio 6.51682; a case where several integer candidates
  tie, so the ratio is 1 and `isFixed()` rejects; and a covariance that is not
  positive definite, where no search is possible and the float vector comes back
  unchanged with ratio 0. The notes' example is an *external* anchor — the
  numbers are theirs, not this implementation's.
- **`tests/test_lambda_resolve.py`**, asserting every number the above prints.
  Needs no dataset and no other program, so it runs on a bare checkout.
- **`tests/test_rtk_equations.py`**, which runs on a bare checkout: the difference
  stage is pure linear algebra, so it is driven from hand-built fixtures through
  `examples/diff_station`. And **`tests/test_rtk_float_regression.py`**, which
  needs the gitignored zero-baseline set and asserts the `rtk:` column
  byte-identical to the output of the original gnssLab-2.2 program.
- `SolverLSQ::getState()`, `getCovMatrix()` and `getUnkSet()`. Only `dxyz` was
  reachable before, which is enough for a position but not for anything that needs
  the ambiguity block — LAMBDA's input is the float solution's covariance, and the
  post-fit residual needs the whole state.
- **`GNSSLAB_DEBUG_SPP`, `GNSSLAB_DEBUG_SOLVER` and `GNSSLAB_DEBUG_RTK`**, matching
  the existing `GNSSLAB_DEBUG_PARSER` / `GNSSLAB_DEBUG_CSMW` pattern, replacing
  three hard-coded `#define debug 1`. All default to 0. Only stdout is affected.

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
- **Figure 8-1: SPP against RTK on one logarithmic axis**, one panel per
  constellation, with both 3-D RMS values and the improvement ratio printed
  inside each panel. The axis is logarithmic because the two curves are 30× to
  330× apart — on a linear axis the RTK band collapses onto zero, which is
  exactly what the figure exists to show. Unlike the other two chapter-8
  figures this one plots two *solutions* rather than three constellations, so
  SPP and RTK take their own two colours instead of the categorical slots; the
  three panels share a y window so that BDS-2's 1.128 m stays visibly worse
  than BDS-3's 0.076 m. All three chapter-8 figures are now embedded in
  [rtk.md](docs/rtk.md), which previously named them without displaying any.

### Changed

- **The cycle-slip detectors' band pair follows the mode, which is what makes the
  Kalman filter usable on BeiDou-3.** `gfObsTypes` and `detectCSMW`'s inline
  table were both hardwired to GPS L1/L2 and BeiDou B1I/B2I, so on `--sys bds3`
  they formed no combination for any satellite and every flag stayed zero -
  silently, since a detector that cannot see a band reports nothing rather than
  failing. `setCycleSlipBands()` now lets the caller give an ordered list of
  candidate pairs per system and takes the first a satellite actually carries;
  `rtk_float` derives it from the mode's own frequency pairs.
  Coverage goes from 0% to 100% on `bds3` and `bds23`, and the Kalman run on
  `bds3` goes from diverging at sod 26538 to RMSE 17 mm, 100% fixed, no epoch
  worse than 1 m. Chapter 7 is untouched: with nothing set, the picker returns
  the same table it always did, and `cs_detect_gf` / `cs_detect_mw`'s output
  directories are byte-identical.
- **`apps/rtk_float --estimator kalman`** — the textbook's 8.3.4, wired for the
  first time. `SolverKalman` and `KalmanFilter` had compiled for years without
  ever being called: the only thing that instantiated them was the lecture
  notes' own program, `examples/exam-8.5-rtk_kal.cpp`, which is not a target.
  The filter carries the ambiguities across epochs, so it needs the other
  parameterisation: `differenceSat`'s five-argument overload keeps the reference
  satellite's ambiguity instead of differencing it away (station-difference
  ambiguities, whose meaning does not change when the reference satellite does),
  and `ambiguityDatum()` removes the resulting rank deficiency with a constraint
  equation. Both had been written and never called. It also needs the cycle-slip
  flags, so `CSDetector` runs on both receivers on this path whether or not
  `--dump-cs` was given.
  What it buys, measured over the zero baseline: the FLOAT solution improves by
  an order of magnitude - GPS 0.177 -> **0.018 m**, BDS-2 1.128 -> **0.098 m** -
  which is the textbook's claim about tying ambiguities across epochs, and the
  one place the earlier "fixing already gives centimetres, a filter adds
  nothing" conclusion was wrong: fixing gives the accuracy, the filter gives the
  float solution. The fixed solutions are unchanged (1.9 / 5.1 / 1.2 mm).
  BDS-3 used to diverge - it tracked to 13 mm for 2076 epochs, then jumped to
  14 m at sod 26538 and never recovered - and the cause was the cycle-slip gap,
  not the filter. A filter's whole premise is that an ambiguity is constant
  across epochs, and that is true only until a slip, so a mode with no flags
  carries a stale ambiguity forever at zero process noise; the least-squares path
  re-estimates every epoch and is immune. The correlation across the three modes
  was exact. Fixing the band pair (above) fixes it completely: bds3 now gives
  RMSE 17 mm, 100 % fixed and no epoch worse than 1 m. The table above is
  measured after that fix, and every mode is now honest.
  Also corrects an earlier claim in the changelog that the filter "adds nothing"
  because fixing already reaches millimetres. That is true of the FIXED solution
  and false of the float one: the single-epoch float solution's accuracy is
  locked to the pseudoranges, and decorrelating the ambiguities across epochs is
  what unlocks it.
  Off by default, so a default run is unchanged.
- **The cycle-slip flags the Kalman solver consumes are now produced, and the
  wiring is tested against known slips.** `CSDetector` was the producer half of
  the chapter-7 -> chapter-8 connection and had never been compiled: the only
  thing that ever called it was the lecture notes' own Kalman program,
  `examples/exam-8.5-rtk_kal.cpp`, which is not a target. It is now an adapter
  over the chapter-7 detectors rather than a second Melbourne-Wübbena
  implementation, it supports BeiDou as well as GPS (its old `else` branch
  deleted every non-GPS satellite), and it is in the library. `rtk_float
  --dump-cs` runs it on both receivers, merges them through the flag-carrying
  `differenceStation` overload, and writes the non-zero flags as
  `<rover>_<sys>_cs.csv`.
  The union of GF and MW is deliberate and now demonstrated: the injected plan
  includes a `(77, 60)` cycle pair, which is the geometry-free combination's
  exact null space, and it is detected - only the MW half can see it. 8/8
  injected slips detected on the zero baseline, and every double-difference
  ambiguity carries a flag.
  Three defects found on the way, all of which made the wiring silently
  useless: the detectors' state tables were keyed by satellite alone, so the
  rover's and the base's recursion windows shared entries; `SPPUCCodePhase::
  linearize` never set `equSys.station` (the other linearizer does) and
  `differenceStation` uses that field to put the station back onto the merged
  keys, so every key carried an empty station and matched no ambiguity at all;
  and the manifest was not valid JSON on Windows, because paths were written
  with unescaped backslashes, which `read_manifest` swallows as "no manifest".
  Known gap, now measured rather than assumed: the detectors' band pair is
  fixed per system (GPS L1/L2, BeiDou B1I/B2I), so `--sys bds3` produces no
  flags at all and a merged `bds23` run covers only its BDS-2 satellites. The
  run reports the uncovered bands by name.
- **`tests/test_rtk_cycle_slips.py`**, which injects known slips into the rover
  file, runs the wiring, and asserts the keys reach every ambiguity and the
  values reach every injected slip.
- **`SPPUCCodePhase::dualCodeTypes` is now a list of code pairs per system**,
  not one pair. Every mode but the merged BeiDou one still lists exactly one, so
  nothing that existed before changes behaviour — the byte-exact anchors in
  `tests/test_rtk_float_regression.py` still pass.
- **`fixSolution` picks the three coordinates out by parameter type** instead of
  assuming they are the first three entries of the non-ambiguity block. That
  assumption held while the block was only ever the coordinates; a merged BeiDou
  solution also estimates the inter-system bias, which sorts before the
  ambiguities, so `dxyzFixed` would have been built from four numbers. It also
  now checks that the ambiguities really are the last block rather than trusting
  it, because getting that wrong mixes a coordinate with an ambiguity silently.
- **Two CI gaps on the chapter-8 side.** The "all targets were produced" check
  had drifted: `rtk_float`, `sync_obs` and `diff_station` were built but never
  checked, so any of them could have silently stopped building. And
  `tests/test_rtk_equations.py` was written for CI and described as CI coverage
  when it landed, but the step was never added — it had only ever been run by
  hand. Both fixed; the new `mlambda` and `test_lambda_resolve.py` are wired in
  as well.
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

- **`ARLambda` — four defects in MLAMBDA ambiguity resolution, three of them on
  the failure path.** `resolve()` fell off the end of a non-void function when
  the integer search failed (`-Wreturn-type`), which is undefined behaviour
  exactly where bad data leads. Worse, both failure paths were *reported as
  success*: `lambda()` swallowed `factorize()`'s and `search()`'s error codes and
  returned 0 with `F` never written, so a caller was handed an all-zero "fixed"
  solution built from an uninitialised matrix — and, because a failed search
  leaves its candidate residuals at zero, the ratio came out 9999.9, i.e. the
  least trustworthy result advertised itself as the most reliable. And the
  `LOOPMAX` guard never fired at all: `search()`'s loop variable `c` was
  redeclared in the `for` statement, shadowing the `c` the guard tests, so the
  out-of-iterations branch was unreachable. Finally, a rank-1 covariance crashed
  the process outright — Eigen's bounds assertion in a debug build, a bare
  segfault without it — which is what `resolve()` now handles by returning the
  float solution unchanged with ratio 0, i.e. `isFixed()` false.
- **`docs/rtk.md` §四 reported the BDS-3 SPP/RTK ratio as 331×; it is 330×.**
  The ratio is 330.4893, and the console banner prints it to one decimal
  (`330.5x`) — the table had rounded that a second time. Recomputed from the
  three full runs with `rtk.error_stats`: 120.82 / 29.61 / 330.49 for
  GPS / BDS-2 / BDS-3, so the other two entries (121×, 30×) were already right.
  It surfaced because figure 8-1 prints the number from the same
  `stats["ratio"]` the table is built from.
- **`RinexObsReader` never delivered carrier phase to anything.** The `L*` branch
  scaled the observation with `data = -data / lambda` and stored it in
  `dopplerMap`. Three things were wrong at once: RINEX phase is in cycles and has
  to be multiplied by the wavelength, not divided (the result was in cycles per
  metre); the sign was inverted; and `satTypeValueData` is assembled from
  `rangeMap`, so phase never reached the observation data at all. Verified against
  the zero-baseline set — `L1C = 116 697 983.8241` cycles times `lambda1 =
  0.190293673 m` is `22 206 887 m`, against a `C1C` of `22 206 873.798 m`, so the
  correct form is `data * lambda` with a positive sign. Invisible until chapter 8
  for three separate reasons: `spp_if` builds its ionosphere-free combination from
  an explicitly named code pair and ignores everything else, the velocity solution
  matches Doppler by the `"D1"`/`"D2"` prefix, and the chapter-7 detectors go
  through the free-function reader in `src/GnssFunc.cpp` rather than this class.
- **A `SYS / # / OBS TYPES` continuation line was read as a new system block, so
  every observation type past the 13th was dropped for every constellation.**
  `strip()` takes its argument by value and returns a new string, and the call
  here discarded the return, so `sysStr` stayed `" "` — non-empty — on a
  continuation line. The code then took the system name and the type count from
  the wrong columns. BDS declares 20 types in the zero-baseline files, so seven
  were lost, among them `C5P`/`L5P` (B2a) — which is what makes the BDS-3
  frequency pair unusable. Both this and the phase bug leave `spp_if`'s two frozen
  baselines byte-identical, which is exactly what the analysis above predicted.
- **`SolverLSQ::getSolution` dereferenced `end()` when the parameter was
  absent**, because the end-of-range test sat inside the loop after the
  dereference; an epoch whose double differences were all dropped produces an
  empty unknown set, which is precisely that case — and it is reached from the
  `catch(...) { continue; }` in `differenceSat`, so it was silent. `getIndex` had
  a matching hole: it returned `varSet.size()` when the variable was not found,
  and the caller used that as a column index, writing one column past the end of
  `hMatrix`. Both now throw. Behaviour is unchanged for every valid input, so the
  frozen baselines are unaffected.
- **A stray `cout << endl;` in `RinexObsReader`'s per-satellite loop**, and two
  unguarded `cout`s in `GnssFunc.cpp` (`differenceSat` and the 6-argument
  `differenceStation`). All three were debris from earlier debug printing and
  could not be switched off.

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
