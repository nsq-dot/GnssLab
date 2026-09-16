# Roadmap and known limitations

## What this does not do

Stated plainly, because a reader should not have to infer it from the source:

- **No RTK.** No carrier-phase relative positioning. The chapter-8 material
  (`ARLambda`, `SolverKalman`, `KalmanFilter`) is present and compiles, but no
  program wires it up.
- **No PPP.** `src/SPPUCCodePhase.*` was removed as unused.
- **No Kalman filter.** `estimator = 2` parses but is ignored; the solver always
  uses least squares.
- **No Galileo or GLONASS.** No observation types are selected for either, so
  the `Galileo` / `GLONASS` config keys are inert.
- **No cycle-slip repair.** `apps/cs_detect_mw` and `apps/cs_detect_gf`
  *detect* slips, via the Melbourne–Wübbena and geometry-free combinations
  respectively; nothing corrects for them, which is why the velocity solution is
  the more robust of the two products.
- **The geometry-free combination is blind to a whole direction.** Because
  `f1/f2 = 77/60` exactly for GPS, `lambda1*77 - lambda2*60 = 0`: a slip of
  `(77k, 60k)` cycles produces no change in `L1 - L2` at all and cannot be
  detected by any GF-based method. Only a second, independent combination (MW,
  or a triple-frequency one) covers it. Demonstrated in
  [cycle-slip-gf.md](cycle-slip-gf.md).
- **Single-frequency ionospheric correction** is Klobuchar only, and only
  applies on the single-frequency path. The default dual-frequency
  ionosphere-free combination removes the first-order ionospheric delay by
  construction.

## Known issues

**Two outputs carry different precision.** `<obs>_<mode>.spp.out` is written at
3 decimals and `<obs>_pos_vel.out` at 6, so the same position differs by up to
0.000499 m between them — the rounding half-step. Harmless, asserted in
`tests/test_baseline_numerics.py` so it is not mistaken for a bug, and documented
in [data-format.md](data-format.md).

**Position RMS uses an approximate reference.** The accuracy figures reference
the RINEX header `APPROX POSITION XYZ`, which is only metre-to-decametre
accurate. The reported position RMS therefore combines repeatability with a
reference-coordinate offset and is **not** absolute accuracy. The internal sigma
is unaffected and is the meaningful precision figure. Resolving this needs an
IGS `.snx` truth coordinate.

**`RinexObsReader` drops observation types past the 13th, and `spp_if` avoids
the bug by luck.** `src/RinexObsReader.cpp:67` calls `strip(sysStr)` and discards
the result — `strip` takes its argument by value — so a continuation line of
`SYS / # / OBS TYPES` keeps a space as the constellation and the declared count
is never applied. The same code in `src/GnssFunc.cpp` was fixed while working on
chapter 7; this copy was left alone because it feeds `spp_if`, whose output is
frozen byte-for-byte in `tests/baseline/`. `spp_if` only ever selects `C1C`,
`C2W` and the Doppler codes, all of which sit in the first 13 GPS types, so the
baseline is unaffected today. A change that selects a type beyond the 13th would
silently lose it. Fixing this needs the baseline to be re-frozen in the same
commit.

**Cycle-slip detector state is function-local `static`.** Per the textbook's
"if you write it as a function, the intermediate state must be `static`", so it
cannot be reset between runs: processing two files in one process carries the
first file's windows and epoch stamps into the second. Harmless for the
one-shot command-line programs, fatal for a test that calls a detector twice.
The fix is to move that state into an object a caller can construct per run.

**The MW output file does not record the one quantity the detector decides on, and
`csFlagArg` is a plotting aid rather than a diagnostic.** `apps/cs_detect_mw.cpp`
writes `mw_m meanMW_m csFlagArg flag`. `meanMW_m` is the running mean *after* the
update, and a detected slip resets that mean to the combination just read — so at
every flagged epoch the stored mean equals `mw_m` and the difference between them
is zero. `csFlagArg` is `csFlag * mwValue`, whose own comment says it is scaled
"to make plotting easier"; it carries no information either way.

The bias the detector actually tests is recoverable in one step, because the mean
carried *into* epoch `i` is the value the file stores for `i-1`. `cycleslip.series_view`
takes its difference against that, which reproduces every decision the detector made
on the chapter's runs (no flagged epoch unexplained, no unflagged epoch above
threshold), so nothing downstream is wrong today.

What is still missing is `varMW`. The detector has two thresholds — a fixed
`minCycles(2.0) * wavelengthMW`, which is exactly 2.0 in the cycles the figures use
and would be a single horizontal line, and an adaptive `4*sqrt(varMW)` — and only
the second can explain a flag that is neither a gap nor a two-cycle step. The first
MW flag in the chapter's own 1 Hz run is a 0.27-cycle wobble that trips the adaptive
test and nothing else, and no figure can show why without it. Emitting `varMW` (and
a meaningful `csFlagArg`) means every `output/cs` run is regenerated and the header
pin in `tests/test_cycleslip_scoring.py` moves with it — worth doing when the MW
figures next need the space.

**`SPPIFCode` has one scalar sigma.** Per-constellation weighting (`noiseBD2Code`,
`noiseBD3Code`) cannot take effect until the class carries a sigma per
constellation.

**`CMAKE_CXX_EXTENSIONS` is ON.** Two `M_PI` uses were replaced with `PI` from
`Const.h` during the restructure, but the build still enables GNU extensions.
Turning them off would be a useful portability check, and would be a step toward
building with MSVC.

**A non-ASCII path crashes every program that touches `<filesystem>`.** Passing a
path containing characters outside the current locale's narrow encoding — a
Chinese directory name, for instance — makes `std::filesystem::path` throw
`filesystem_error: Cannot convert character sequence`, and nothing catches it, so
the process aborts with no diagnostic. It affects `spp_if`, both cycle-slip
detectors and the two new `bias.ini` programs, all of which call `fileExists()`
or `ensureDirectory()` on a path that came from the command line or a config
file. Reproduce with `build/bin/spp_if.exe /d/中文目录/x.ini`. The fix is to
convert through `std::filesystem::u8path`, or to catch at the call site; note
that the development machine's own `%TEMP%` is under a Chinese user name, so
anything that puts scratch files there will hit it.

**`system_bias` writes `tx_backward_verify.csv` twice over.** `apps/system_bias.cpp`
calls `verifyTransmitTime(...)` with identical arguments at two consecutive
statements, so every row of that file is written twice and the row count is
double what it should be. Left in place deliberately: it changes output, and the
commit that fixed the hardcoded paths had to prove that output was unchanged.
Removing the duplicate call halves the file.

**`read_rinex`'s summary report always prints zeros.** The `sat_G`, `sat_C`,
`sat_E` and `sat_R` sets and the `obs_count` map in `apps/read_rinex.cpp` are
declared and printed but never inserted into, so the on-screen report shows `0`
satellites per constellation and an empty observation-type list, while the CSV it
writes is correct. Same reason for leaving it: the report is part of the output
that the portability commit had to leave byte-identical.

**The ephemeris programs have no "all satellites" or "all epochs" mode.**
`bds_eph` compares one satellite at one epoch and `bds_gps_diff` sweeps an
explicit list, because `RinexNavStore` and `SP3Store` are queried per satellite.
A satellite set derived from the SP3 header's `+` lines would remove the need for
`satList` in `config/eph.ini` and stop a run against the trimmed sample from
silently covering only the satellites the sample happens to carry.

## Possible next steps

Roughly in order of value per unit of effort:

1. **Elevation-dependent weighting.** Currently all observations carry equal
   weight. A `1/sin(el)` or exponential elevation model is a small change to
   `SPPIFCode` and usually improves the vertical solution most.
2. **Per-constellation sigma.** Extend `SPPIFCode` to hold one sigma per
   system, which activates the reserved config keys and is the prerequisite for
   meaningful GPS/BeiDou weighting.
3. **Wire up the Kalman filter.** `src/SolverKalman.*` and
   `src/KalmanFilter.*` compile and are in the library; `estimator = 2` is
   already parsed. This would give a kinematic solution instead of per-epoch
   least squares.
4. **Absolute accuracy assessment.** Add an option to read an IGS `.snx`
   solution and report against it, which would let the README quote a true
   accuracy rather than a repeatability figure.
5. **Cycle-slip repair**, not just detection, which would extend the usable
   carrier-phase work.
6. **CI on Windows and macOS** alongside Linux, which would exercise the MinGW
   path that the static-runtime fix in `CMakeLists.txt` addresses.

Items 1 and 2 are the smallest changes with the clearest effect on the numbers
the README quotes, and either would make a good first contribution.
