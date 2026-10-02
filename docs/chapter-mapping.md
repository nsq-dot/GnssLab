# Course chapter mapping

This project began as a course assignment built on the *gnssLab-2.4* teaching
framework, whose example programs were named `exam-<chapter>.<section>-<topic>.cpp`
so that the file list read in the same order as the lecture notes.

The restructure renamed those files to conventional engineering names, which
loses that property. This table restores it: it maps each lecture chapter to the
program that demonstrates it, keeping the correspondence with the accompanying
project report even though the filename no longer carries the chapter number.

## Programs

| Chapter | Original filename | Current path | Status |
|---|---|---|---|
| 1.1 | `exam-1.1-parse_opt.cpp` | `examples/parse_opt.cpp` | kept |
| 1.2 | `exam-1.2-parse_config.cpp` | `examples/parse_config.cpp` | **written here** — the original target listed only `ConfigReader.cpp`, which has no `main()`, so it could never link |
| 2.1 | `exam-2.1-eigen.cpp` | `apps/matrix_calculator.cpp` | rewritten as a command-line matrix calculator |
| 2.2 | `exam-2.2-time_convert.cpp` | `examples/gpst_to_utc.cpp` | renamed; GPS → UTC |
| 2.2 | — | `examples/bdweek_to_commontime.cpp` | added: BDS week/second → CommonTime |
| 2.2 | — | `examples/jd2020_test.cpp` | added: BDS week/second → JD2020, round-trip |
| 2.3 | `exam-2.3-coord_convert1..5.cpp` | — | removed; superseded by `examples/ecef_enu_test.cpp` and the `src/CoordConvert.h` library functions |
| 2.3 | — | `examples/ecef_enu_test.cpp` | consolidated version of the five coordinate examples |
| 2.4 | `exam-2.4-skyplot.cpp` | — | removed; sky-plot rendering moved to the Python layer as figures |
| 3.1 | `exam-3.1-satid.cpp` | — | removed; `SatID` / `ObsID` are exercised throughout `src/GnssStruct.h` |
| 3.2 | `exam-3.2-read_rinex_data.cpp` | `apps/read_rinex.cpp` | renamed; **made configurable** (`config/bias.ini`) |
| 4.1 | `exam-4.1-gps_eph.cpp` | `apps/bds_eph.cpp` | rewritten for BeiDou, extended to compare against SP3, and **made configurable** (`config/eph.ini`) |
| 4.1 | — | `apps/bds_gps_diff.cpp` | added: broadcast vs precise orbit/clock, full constellation; **made configurable** (`config/eph.ini`) |
| 5.1 | `exam-5.1-system_bias.cpp` | `apps/system_bias.cpp` | renamed; **made configurable** (`config/bias.ini`) |
| 6.1 | `exam-6.1-sppif.cpp` | `apps/spp_if.cpp` | **extended** with `src/SPPVelocity.*` (Doppler velocity), a configuration file, and a command line |
| 7.1 | `exam-7.1-cs_detect_mw.cpp` | `apps/cs_detect_mw.cpp` | **fixed and made configurable** — it could not run at all: the data directory string was missing its trailing separator, and three bugs in the free-function RINEX reader (`src/GnssFunc.cpp`) kept the header from ever finishing. Also corrected the BeiDou observation types, which selected band 6 instead of B1I/B2I |
| 7.3 | — | `apps/cs_detect_gf.cpp` | **written here** — geometry-free combination, two detectors (epoch difference and polynomial fit), with `detectCSGFdiff` / `detectCSGFpoly` in `src/GnssFunc.cpp`. Exercises 1–3 are worked in [cycle-slip-gf.md](cycle-slip-gf.md) |
| 8.1 | `exam-8.1-sync_obs.cpp` | `examples/sync_obs.cpp` | **written here** — the original was a copyright header and nothing else, no code at all. Demonstrates the epoch-alignment rule (now `src/EpochAlign.h`) that pairs a rover epoch with a base epoch, including the case where the base has already passed it and `SyncException` is thrown |
| 8.2 | `exam-8.2-diff_station.cpp` | `examples/diff_station.cpp` | **written here** — also an empty stub. Drives `differenceStation` / `differenceSat` on hand-built equation systems and prints what they actually do: `Parameter::iono` is deleted rather than differenced, the receiver clock cancels, the ambiguity coefficient is carried through unchanged, and rank deficiency yields a plausible wrong answer rather than an error |
| 8.3 | `exam-8.3-lambda.cpp` | `examples/exam-8.3-lambda.cpp` (built as `mlambda`) | **fixed and built** — MLAMBDA ambiguity resolution on the notes' own example 8-1, plus the two outcomes that are not a plain fix (a ratio that fails the test, and a covariance no search can use). `ARLambda` had four defects, the worst being that both failure paths reported success and the search's `LOOPMAX` guard was dead; a rank-1 covariance crashed the process. Asserted by `tests/test_lambda_resolve.py` |
| 8.4 | `exam-8.4-rtk_lsq.cpp` | `apps/rtk.cpp` | **made into a program** — relative positioning with a least-squares or a Kalman estimator, four constellation/frequency modes (`--sys gps\|bds2\|bds3\|bds23`), integer ambiguity resolution, an optional BeiDou inter-system bias, a config file, a per-epoch diagnostics CSV and a manifest. Both exercises and the filter are worked in [rtk.md](rtk.md) |
| 8.5 | `exam-8.5-rtk_kal.cpp` | `apps/rtk.cpp --estimator kalman` | **its functionality, not its file** — the Kalman filter, the reference-satellite constraint and the cycle-slip wiring all live in the chapter-8 program rather than as a second one, because the upstream file is a hardcoded teaching transcript (its data path is the author's own machine) and reviving it would duplicate the program's scaffolding. See §10 of [rtk.md](rtk.md). The file itself is kept for provenance and is not built, like `exam-9.1`/`exam-9.2` below |
| 9.1 | `exam-9.1-gps_compare.cpp` | — | kept, not built: an upstream source with a hardcoded data path, superseded by `apps/bds_gps_diff.cpp` |
| 9.2 | `exam-9.2-bds_compare.cpp` | — | kept, not built, same |

## Where the work for this project sits

The chapters that carry original work rather than a rename:

| Chapter | Component | Files |
|---|---|---|
| 6 | BeiDou broadcast ephemeris: orbit and clock, with GEO/IGSO/MEO handling | `src/NavEphBDS.hpp`, `src/NavEphBDS.cpp` |
| 6 | Doppler velocity: weighted least squares with robust outlier rejection | `src/SPPVelocity.h`, `src/SPPVelocity.cpp` |
| 5 | Systematic-error diagnostics: TGD, ionospheric and tropospheric delay | `apps/system_bias.cpp` |
| 7 | Melbourne–Wübbena cycle-slip detection | `apps/cs_detect_mw.cpp` |
| 7 | Geometry-free cycle-slip detection, and the injection/validation harness for exercises 1–3 | `apps/cs_detect_gf.cpp`, `scripts/inject_cycle_slips.py`, `scripts/check_cycle_slips.py`, [cycle-slip-gf.md](cycle-slip-gf.md) |
| — | Configuration system: four profiles (`spp.ini`, `cs.ini`, `bias.ini`, `eph.ini`), one struct + `defaults()`/`fromIni()` pair each | `src/ConfigData.*`, `src/ConfigReader.*`, `config/` |
| — | Accuracy analysis, plotting, and the unified CLI | `python/` |

## Library modules by chapter

| Chapter | Module |
|---|---|
| 2 | `src/TimeStruct.*`, `src/TimeConvert.*` (time systems) |
| 2 | `src/CoordStruct.h`, `src/CoordConvert.h` (coordinates) |
| 3 | `src/GnssStruct.*` (core data types), `src/RinexObsReader.*` |
| 4 | `src/RinexNavStore.*`, `src/NavEphGPS.*`, `src/NavEphBDS.*`, `src/SP3Store.*`, `src/Rx3ClockReader.*` |
| 5 | `src/GnssFunc.*` (satellite position/clock, error models) |
| 6 | `src/SolverLSQ.*`, `src/SPPIFCode.*`, `src/SPPVelocity.*` |
| 7 | `detectCSGFdiff()`, `detectCSGFpoly()` and `detectCSMW()` in `src/GnssFunc.cpp`; `apps/cs_detect_gf.cpp`, `apps/cs_detect_mw.cpp` |
| 8 | `src/SolverKalman.*`, `src/KalmanFilter.*` (the Kalman estimator), `src/ARLambda.*` (MLAMBDA), `src/SPPUCCodePhase.*` (the linearizer), `src/CSDetector.*` (the cycle-slip flags the filter consumes), and `fixSolution` / `ambiguityDatum` / the two `differenceStation` / `differenceSat` overloads in `src/GnssFunc.cpp` |
