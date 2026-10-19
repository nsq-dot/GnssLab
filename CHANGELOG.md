# Changelog

Notable changes to this project. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

One structural note. Everything after 1.0.0 accumulated under an `[Unreleased]`
heading and was tagged twice from that same block - `v1.1.0` (chapters 6 and 7)
and `v1.2.0` (chapter 8) - so no single section describes either release on its
own. The 1.2.0 section below is that block frozen, and it therefore carries both.
Releases get their own section from here on.

## [Unreleased]

### Added

- **A Bias-SINEX/OSB reader, and `rtk --osb`, which makes BeiDou precise
  products work.** `src/SinexBiasStore.*` reads IGS Bias-SINEX v1.00 in
  ABSOLUTE mode: OSB records only (DSB, station-specific and phase records are
  counted and skipped), fixed-width or whitespace-separated, units `ns`/`s`/`m`
  converted to metres, half-open validity intervals with multiple intervals per
  satellite/signal, and the format's `bias = observed - true` sign. The new
  `sinex_bias` program (`gnss app sinex-bias`) prints the file's coverage and
  bounds and can push a synthetic observation through the same correction hook.
  `apps/rtk.cpp` gains `--osb <file>` (config key `osbFile`), accepted only with
  `--sp3` and `--clk`: `SPPIFCode::correctCodeBias` subtracts the per-satellite,
  per-signal bias from the **raw three-character** code observables before
  `convertObsType()` collapses them, so C2I/C7I/C5P stay distinguishable. Phase
  biases are deliberately not applied (the ambiguities absorb them and the
  double difference cancels them). A startup coverage check rejects a file that
  lacks a signal the mode observes - which is how GFZ's MGEX OSB file is caught:
  it carries only BeiDou C2I/C6I, not C7I or C5P. Signals the file does not
  cover are left uncorrected and counted in the manifest rather than dropping
  the satellite, so the RTK equations keep their geometry. The manifest now
  records the SP3, CLK and OSB paths and the correction/missing counters.

  Measured on the chapter's zero baseline with the WHU/WUM 2022-062 set (7934
  epochs, `--trop`, `--fix`, true antenna coordinate): the SPP 3-D RMS goes from
  13.102 to **6.588 m** on BDS-2 and from 6.280 to **2.530 m** on BDS-3, both
  better than the broadcast baselines (13.502 / 3.124 m); GPS improves from
  2.824 to **2.346 m**. The WHU OSB has no BDS-2 GEO (C01-C05) records, so 23412
  code observations stay uncorrected. The RTK columns barely move - float 3-D
  RMS within 1 mm, the fix/no-fix decision identical on every epoch, accepted
  fixed positions within 5 cm (BDS-2) and 9 mm (elsewhere) - because a satellite
  bias cancels in the double difference; the residual movement is the changed
  SPP linearization point. The chapter's §4 table and the four figure-8-1
  panels now use the WUM+OSB configuration; §8 onward keep the broadcast-BDS
  tree because their double-difference results are unchanged. `docs/rtk.md` §4
  has the tables and the reasoning; `tests/test_sinex_bias.py` (synthetic
  fixtures, in CI) and `test_rtk_osb.py` (data-gated) lock the reader and the
  measured result.

- **Per-system and per-orbital-family code weighting for the RTK solution.**
  Six multipliers (`codeSigmaFactorGPS/BD2/BD3` and `...GEO/IGSO/MEO`, also
  available as `--sigma-factor-*`) scale the linearizer's 0.3 m code sigma:
  `effective sigma = 0.3 m * systemFactor * orbitFactor`. They are applied to
  the linearized equations before the station and satellite differences, so
  they reach the double-difference weights (including `--dd-cov`) but leave the
  `spp:` column alone. All default to 1.0, so every existing output is
  byte-identical; the manifest records the six values under `codeSigmaFactors`.

  On the chapter's zero baseline the mixed float 3-D RMS fell from 0.0866 m to
  0.0621 m with `GPS=2.0, BD3=0.7, MEO=0.7` (fixed solution unchanged at
  0.0120 m, because phase weighting is untouched), and the weighted mixed
  Kalman went from 0.0152 to 0.0143 m. The effect holds on every third of the
  run: leave-one-third-out validation reduced the held-out segment by 19-22%
  against the default mixed solution. Downweighting BDS-2 made the float worse
  (0.0934 m at factor 2), which is the same lesson as the quota ablation - it
  carries geometry and redundancy. The measured factors are dataset-specific;
  `docs/rtk.md` §12.4 has the full tables and the cross-validation.

- **Mixed GPS + BeiDou RTK (`--sys gpsbds`), with one reference satellite per
  constellation and a BeiDou orbital-family quota.** The old `differenceSat()`
  anchored every double difference on a single global reference satellite and
  looked its equations up by `(system, obsType)`, so a GPS reference silently
  dropped every BeiDou satellite. The mixed mode carries a `constellation ->
  satellite` map, picks the highest-elevation survivor per constellation, and
  adds one ambiguity constraint per reference on the Kalman path. No
  GPS/BeiDou inter-system bias is needed: receiver clocks are already estimated
  per system and the two blocks are tied only through the coordinates. BeiDou
  satellites are classified GEO / IGSO / MEO from the broadcast elements
  (`sqrt_A` plus `i0`, with the PRN assignment as a fallback - C59/C60 are GEO
  despite their PRN), and `bdsOrbitRatio` / `bdsMaxSats` select how many of each
  family take part; within a family the highest elevations win. `--dump-orbit-types`
  prints the classification.

  Measured on the chapter's zero baseline (7934 epochs, broadcast for both
  systems, `--fix --trop --bds-tgd --base-xyz`): the no-cap mixed solution is
  0.0866 m float / 0.0120 m fixed against GPS-only 0.1752 / 0.0224, and its
  Kalman float is 0.0152 m. It does **not** beat the BDS-only `bds23` float
  (0.0730 m), because every system still shares one code sigma and the noisier
  GPS code pulls the float back; per-system weighting is the next step. The
  family ablation is in `docs/rtk.md` §12: MEO is indispensable (without it the
  float collapses to 0.2228 m and the fix rate to 82.2%), GEO is neutral here,
  and a satellite cap costs more than the ratio gains - the measured "right
  ratio" on this dataset is no cap, all three families.

- **Figures 8-1b/c/d, 8-4b/c/d and 8-6b/c/d: figures 8-1, 8-4 and 8-6, each
  redrawn one direction at a time.** The chapter's parent figures fold the three
  components of an ENU error into one magnitude or one shared axis; the nine
  siblings undo that split - **one direction per figure**, b/c/d standing for
  east, north and up - so that the y scale can follow the component under
  discussion. What each family then *draws* is its own business, and the two
  differ. **8-1b/c/d draw exactly the epochs figure 8-1 draws** - the same run,
  stopped at the same cap, undecimated, no markers; what they add is the *sign*,
  which the parent's magnitude cannot carry (over the span drawn the
  single-point solution's vertical mean swings several metres either side of
  zero - GPS -2.6 m to +2.1 m, BDS-3 -0.7 m to +3.5 m), and the cap and the mask live
  in `figures._in_8_1_span`, shared with the parent, so the four cannot drift
  apart about which epochs the chapter is talking about. **8-4b/c/d and 8-6b/c/d
  take a short window each instead**, because their subject is invisible over a
  whole run: the Kalman filter settles inside its first few hundred epochs and is
  a pixel wide on 7934 of them. Those six take 101-123 epochs out of the same
  outputs, decimated to every 4th or 16th and drawn with a marker on every
  survivor (8-6b/c/d the opening 400 s, where the transient lives; 8-4b/c/d a
  long mid-run stretch, so BDS-2's rejections read as repeated gaps).

  The coarse series turns blue in all nine while the subject stays red, so each
  curve stays followable; the legends are unchanged and only the captions say
  which colour is which. **No solver is re-run** - a window and a cap are both
  slices of the existing output, which `--stop` would reproduce at the cost of a
  second run tree - and **no option was added**: the windows, the cap and the
  decimation are constants in `figures.py`, and each figure rides its parent's
  gate (`--no-figures`, `--no-fix-figures`, `--kalman-out`).

  **Both series stay in the same panel on one shared axis; what is split is the
  direction.** An earlier revision of these figures gave each series a lane of
  its own, one above the other, each scaled to itself, and that is what this one
  replaces. Two lanes on two scales cannot be read against each other at all,
  and the vertical distance between them is not a quantity worth reading anyway:
  SPP is an absolute solution and RTK a differential one on a zero baseline, so
  their being 12-40x apart says what each solves for, not that two algorithms
  disagree. Splitting by direction instead puts the size difference where it can
  do no harm - the ENU components span an order of magnitude (BDS-2's error is
  nearly all vertical), so one figure holding all three would have to squash
  one of them, and each of the nine carries its per-curve bias/sigma/RMS in the
  corner for the comparison a shared axis cannot make. It also puts the
  convergence where a reader can find it: **on this dataset the Kalman filter's
  transient is a vertical phenomenon** - over BDS-2's first ten epochs the error
  is +0.68 m in U, -0.21 m in N and only -0.04 m in east, the same order as its
  own settled noise - so 8-6b alone would suggest the filter had no transient at
  all, while 8-6d shows the whole of it. The cause is geometry: BDS-2 is an IGSO
  constellation with the weakest vertical observability of the three.

  The six windowed figures fall back to drawing the whole series, undecimated,
  say so on stdout, and drop the window from the title rather than promising
  seconds their axis does not carry; 8-1b/c/d have no fallback to make and name
  their cap in the title and in the x label instead, since the run carries on
  past it. Six of the seven full-span `vis_rtk_*.png` are byte-identical to
  before - figure 8-1 itself is not, see Changed - and the exact filename
  assertions in `tests/test_rtk_plot.py` moved with the new names.

- **Chapter 8's exercise 3: the double differences now carry their covariance,
  and it changes almost nothing.** `differenceSat` weighted each double
  difference as if it were independent — `1/(1/w_rover + 1/w_base)` — although
  every DD of one observation type shares the datum satellite's single
  difference, which leaves a covariance `q_datum` between pairs. Behind
  `--dd-cov` it now builds `Q_DD = C·Q_SD·Cᵀ`, block-diagonal per observation
  type (the diagonal is exactly the old variance-addition weight; only the
  off-diagonal is new), and both solvers take its inverse as the weight matrix.
  `EquSys::obsCov` being empty leaves the old diagonal path bit-identical, so
  every byte-exact anchor survives; `fixSolution` and LAMBDA needed no change at
  all, because they already consume the full `Q_N` and the correlation reaches
  it by itself. Measured on the chapter's zero baseline: the SPP column does not
  move (it is computed before the differencing — a clean control), the float 3-D
  RMS moves 5–8%, the ratio median LAMBDA consumes moves 5–12%, and **no fix
  decision flips** (the fixed fraction, the fixed RMS and the worst accepted
  error are unchanged to the millimetre). An effect of order 1/n with n = 6–8
  satellites is what the theory predicts; the ratio's own margin is what keeps
  it away from the decisions. `examples/diff_station --dd-cov` prints the matrix,
  and `tests/test_rtk_equations.py` pins it against a hand-computable case.

- **Figure 8-7, and `rtk-plot --dd-cov-out`.** The new figure draws the ratio
  test statistic — the quantity the fixing decision actually turns on — for the
  same configuration solved with and without the double-difference covariance,
  one panel per constellation. The second tree is named by a flag rather than
  inferred, for the same reason figure 8-6's is: `apps/rtk.cpp` writes
  `<rover>_<sys>_rtk_float.out` for both weightings, so two of them in one tree
  silently overwrite each other. Which side is which is read from each run's
  manifest (the `ddCov` field; absent means independent, via
  `rtk.DEFAULT_DD_COV`), never from argument order, and a constellation whose
  two runs agree on the weighting is dropped rather than drawn against itself.

- **Figure 8-6, and the section it belongs to.** Chapter 8's Kalman section had
  a table and no figure, while every neighbouring section had one. The new
  figure draws the single-epoch least-squares float solution against the
  filter's, one panel per constellation, on figure 8-1's own log window - so the
  three can be read against each other. Gone through `gnss rtk-plot
  --kalman-out DIR`, which names a second output tree because `apps/rtk.cpp`
  writes `<rover>_<sys>_rtk_float.out` for both estimators and two of them in
  one tree silently overwrite each other. Which run is which is read from each
  manifest; a manifest that predates the `estimator` field is taken as
  least-squares, which is the only reading that pairs the chapter's existing
  runs. The six new strings went into both language tables, and the new tests
  follow the existing run-twice-and-compare-hashes pattern.

  Drawing it turned up something the table had hidden: **the filter's gain is
  not uniform in time.** BDS-3's first ~1000-1500 epochs gain 1.4-1.6x, not the
  4-11x the section quoted; the second half of the same runs gains 37-89x. The
  section now says so, with a second table for the converged window and a fourth
  conclusion for the transient, rather than leaving the figure and the prose to
  disagree.

- **Figure 8-1 draws both columns raw and annotates the offset that separates
  them, and two opt-in corrections went into `SPPUCCodePhase` to investigate
  what that offset is.** The panel is the graphical form of the console table's
  rightmost column - SPP 3-D RMS 21.4 / 33.4 / 25.0 m against RTK's 0.177 /
  1.128 / 0.076 m, i.e. 121x / 30x / 330x - and the axis says *deviation from
  the reference* rather than error, because that is what it measures. Two
  caveats travel with it, both on the panel as the printed offsets: the RTK
  column is `rtk: = xyzRover + dxyzRTK` against a base station that *is* the
  reference coordinate, so its near-zero offset is structural rather than a 2 cm
  accuracy claim; and most of the SPP column is its own 19.5 / 28.2 / 23.7 m
  offset. `docs/rtk.md` §4 spells both out, and withdraws the claim that the
  SPP/RTK ratio is reference-independent - the datum term is fully present in
  one column and zero by construction in the other.

  Centring each series on its own mean was tried and kept **out** of the figure:
  it answers a different question (how repeatable is each solution) than the one
  the figure is for (where the two solutions land), and the chapter's headline
  ratio is the raw one. The centred numbers are in §4's table and prose, not
  drawn: **50x / 16x / 110x** for the residual RMS, and **7.2x / 9.2x / 14.4x**
  for the epoch-to-epoch difference, which is the only one of the three that
  deserves the word "noise".

  The investigation added `enableTrop` / `enableBDSTGD` to `SPPUCCodePhase`
  (config keys, `--trop` / `--bds-tgd`), both **off by default** so the
  byte-exact anchor survives - `SPPIFCode`'s constructor initialises both flags
  to true and the derived class never reset them, so the RTK wiring sets them
  explicitly. Measured: the troposphere correction makes the SPP column *worse*
  (19.5 -> 24.9 m on GPS), the BeiDou TGD correction cuts its scatter 2.7-3.5x
  but leaves its offset, and neither moves the conditioning. Also added
  `sppNObs/sppNUnk/sppRank/sppCond` to the RTK diagnostic CSV. The ~19-28 m SPP
  offset was later found to be the receiver-reported datum - see the entry under
  Fixed.

  **Follow-up, same day: that offset is not a constant, and what it leaves
  behind is not noise.** Binning the run into eighths, GPS's SPP vertical error
  goes -24.1 -24.7 -19.7 -19.6 -13.5 -8.1 -3.8 -1.7 m - a 22 m monotone drift
  over 2.2 hours - while the same column's epoch-to-epoch vertical sigma is
  0.74 m, so ~99% of the 8.788 m residual RMS is drift. That drift is the clue
  the earlier exclusions all miss, since every one of them (TGD, broadcast
  orbits, troposphere) was tested against a constant; §11 now records it
  separately from the offset. Measured for the record: `--trop` shifts the level
  by ~5.8 m and leaves the drift's shape and amplitude untouched (22.8 m against
  22.4 m). That "worse" was measured against the wrong datum and does not hold -
  against the true one the correction helps, see Fixed.

### Changed

- **The RTK diagnostics carry the mixed-solution state.** Six columns are
  appended to `_rtk_diag.csv`: `datumSats` (every reference satellite, e.g.
  `C24;G01`), the candidate counts `bdsCand{GEO,IGSO,MEO}` and the selected
  counts `bdsSel{GEO,IGSO,MEO}`. The legacy `datumSat` column is unchanged and
  reports the GPS reference when there is one. Single-system runs write empty /
  zero in the new columns and their existing columns, `.out` files and byte
  anchors are untouched. The manifest gains `systems`, `codePairsBySystem`,
  `bdsOrbitRatio` and `bdsMaxSats`. `docs/rtk.md` gains §12 for the mixed mode
  and the later sections renumber accordingly.

- **Figure 8-1 stops drawing at sod 31200, 1378 epochs before its run ends.**
  The tail is dropped on request, not because the data stops there: measured
  against the chapter's reference, the single-point solution degrades badly
  across it (3-D RMS 2.84 -> 4.54 m on GPS, 13.50 -> 16.48 m on BDS-2 and
  3.12 -> 5.50 m on BDS-3, BDS-2 reaching 51 m). **Only the drawn series is
  clipped.** The bias/sigma/RMS printed in the panels are still the caller's,
  over all 7934 epochs, so they keep agreeing with the chapter's tables, and the
  x label now says where the axis stops (`axis_sod_to`) so that a shorter curve
  cannot be read as a shorter run. Figures 8-1b/c/d stop at the same cap through
  the same helper, and are renamed `vis_rtk_spp_vs_rtk_dir_{e,n,u}_ts.png` from
  `..._zoom_{e,n,u}_ts.png` - this release makes them draw their parent's whole
  span, so they are no longer zooms, and a name that no longer describes the
  figure is the defect this project's captions exist to avoid.

- **Section four's runs now correct the troposphere and, for GPS, use precise
  orbits and clocks.** The SPP column becomes 2.844 / 13.502 / 3.124 m (from
  11.314 / 18.673 / 9.104) and the ratio 16x / 12x / 40x. The three levers,
  measured over the same epochs: the true base position (21.36 -> 11.31 m 3-D
  RMS), `--trop` (-> 9.25, and the mean offset 7.13 -> 1.04 m), and precise
  products for GPS (-> 2.84 m, mean offset 0.01 m). BeiDou stays on the
  broadcast ephemeris because the CODE product at hand carries no BeiDou
  clocks; the RTK column is unaffected by that asymmetry, so section five's
  constellation comparison still holds. Every repro command in the document
  carries the new options.

### Fixed

- **Every chapter-8 table is now in the same configuration as the figures.**
  Section four's runs were switched to the troposphere correction and GPS
  precise products, and its figure and SPP column were regenerated with them -
  but the tables in sections 8, 9 and 10 kept numbers from the runs *without*
  `--trop`, so the document disagreed with itself: figure 8-4's panel annotates
  a fixed 3-D RMS of 0.0180 / 0.0304 / 0.0114 m while the table directly beneath
  it said 0.0019 / 0.0051 / 0.0011 m. Both are reproducible (removing `--trop`
  reproduces the old table to the digit, which is how the mismatch was
  identified), but only one configuration belongs in one chapter. Everything is
  now the corrected one, and the conclusions that move are spelled out: the
  fixed solution's gain over the float one is **7–37x** rather than 70–222x (it
  inherits the float solution's 2–3 cm datum offset), the Kalman filter's gain
  is **3–11x** over the whole run and 5–26x once converged (was 4–11x / 37–89x),
  and the residual-ratio line that did *not* move (3.7 / 5.1 / 3.3x) is now
  labelled as the one that measures noise.

- **Two commands in the chapter did not do what the chapter said.** The plot
  one-liner that "generated this document's figures" had no `--ref-xyz`, so on a
  machine that has `data/` it falls back to the base station's RINEX header -
  the coordinate measured to be 24.9 m from the antenna - and draws a different
  figure from the committed one; the reproduction section now passes the true
  position. And the GPS run lines wrote `--trop $SP3 $CLK` where the program
  takes `--sp3 FILE --clk FILE`, which does not run at all. Both trees were
  re-derived from the corrected commands and compared byte for byte.

- **The console's two notes about the comparison table said the SPP offset was
  still unexplained.** It is not — it was the receivers' own header coordinate
  all along (`rtk.md` §13) — and on the corrected runs it is 0.01 / 9.4 / 1.7 m
  rather than 19–28 m. Both notes, in both languages, now say what the document
  says: the RTK column is tied to the reference by construction, the SPP column
  is not, and the ratio inherits that asymmetry.

- **The chapter's SPP column was measuring the reference coordinate, not the
  solution.** Both receivers report header coordinates 0.070 m apart that sit
  **24.9 m** from the antenna. Three measurements separate the two: the same
  solver reaches **1.06 m** on a 2025 station whose true coordinate is known;
  the two receivers solving independently on precise orbits and clocks agree to
  **0.020 m**; and the broadcast and precise solutions differ by **1.70 m**. So
  the 19-28 m was the datum. `rtk` gains `--base-xyz X,Y,Z` to supply a true
  base position - the data files keep the receiver-reported values, which is
  what the byte-exact anchor was produced with, and which is itself the finding.
  With it the SPP 3-D RMS becomes 11.3 / 18.7 / 9.1 m (from 21.4 / 33.4 / 25.0),
  the RTK column is unchanged, and the chapter's tables, figures and repro
  commands all carry the true value. The claim that the troposphere correction
  makes the SPP *worse* is also withdrawn: measured against a true datum it
  helps (11.3 -> 9.2 m 3-D RMS, mean offset 7.1 -> 1.0 m).

- **The precise clock products are not relativity-corrected.** The --sp3/--clk
  path turned SP3Store::addRelativity off, on the premise that the IGS/CODE
  clocks already contain that term. They do not - they are the same convention
  as the broadcast polynomial - and the comparison that "verified" the premise
  was circular, using the store's own computed term. Measured against bds_eph
  the precise clock minus the broadcast polynomial is 1.9 / 10.3 / -2.8 ns, the
  broadcast clock's own error, not the +/-7-25 ns the term is worth. With it
  back, the 2025 Wuhan 24-hour run goes from 6.32 m mean / 14.80 m 3-D RMS to
  1.06 m / 4.94 m, better than the broadcast ephemeris as a precise solution
  should be.


- **The withdrawn claim that the SPP/RTK ratio is reference-independent was
  still being printed.** `docs/rtk.md` withdrew it and the CHANGELOG recorded
  the withdrawal, but the console report's headline still read "the one
  reference-independent number here" and its note 2 still argued the case - the
  one place a reader is most likely to meet the number. Both languages fixed,
  along with three docstrings in `gnss_plot.rtk` that asserted the same thing,
  and the headline no longer calls the raw ratio an "improvement". Two tests
  now pin the corrected wording; note 1 gained the sentence that its datum
  argument covers the RTK column only.

- **`CoordConvert.h`'s inline functions defaulted `debug` to 1, so every
  translation unit that included the header before defining its own `debug`
  printed unconditionally** - which is this repository's usual order. The
  header's inline `elevation()` therefore put 10839 `refBLH/cosUp/elev` lines
  into `spp_if`'s stdout (of 23417), and made the new troposphere path in
  `rtk --trop` print a line per satellite. Default changed to 0; `spp_if`'s
  stdout is byte-identical to before minus those lines, and the frozen `.out`
  products are untouched. The `elaptc/af0/dtc` flood from
  `NavEphGPS::svClockBias` is a separate, unguarded set of prints in the library
  and was deliberately left alone.

### Changed

- **The chapter-8 program is renamed `rtk_float` -> `rtk`.** It had stopped
  being a float-only program: `--fix` resolves the ambiguities to integers,
  `--estimator kalman` carries them across epochs, `--sys bds23 --isb` merges the
  two BeiDou generations, and `--dump-cs` produces the slip flags the filter
  consumes. The name described the first of those and hid the rest.
  The OUTPUT files keep their `_rtk_float.out` suffix on purpose: that file holds
  the float solution whatever estimator produced it, and its name is pinned by
  `tests/test_rtk_float_regression.py`'s byte-exact comparison with the original
  program's output. Which estimator ran is recorded in the manifest, and
  `--help` says so. `gnss app rtk-float` becomes `gnss app rtk`.
  Along the way, three documents that had gone stale got corrected rather than
  just renamed: `docs/roadmap.md` still claimed the RTK program was float-only
  with no fixing and no filter, `docs/chapter-mapping.md` still listed chapter
  8.5 as "next batch", and neither mentioned exercise 3 or the cross-constellation
  gap that remain.

## [1.2.0] — 2026-10-02

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
