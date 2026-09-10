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
- **No cycle-slip repair.** `apps/cs_detect_mw` *detects* slips via the
  Melbourne–Wübbena combination; nothing corrects for them, which is why the
  velocity solution is the more robust of the two products.
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

**`CSDetector` is a parallel implementation.** `src/CSDetector.*` is a
class-based cycle-slip detector; `apps/cs_detect_mw.cpp` calls the free function
`detectCSMW()` in `src/GnssFunc.cpp` instead. Both compile. Wiring the class in,
or removing it, would resolve the ambiguity — as it stands a reader may not
realise there are two.

**`SPPIFCode` has one scalar sigma.** Per-constellation weighting (`noiseBD2Code`,
`noiseBD3Code`) cannot take effect until the class carries a sigma per
constellation.

**`CMAKE_CXX_EXTENSIONS` is ON.** Two `M_PI` uses were replaced with `PI` from
`Const.h` during the restructure, but the build still enables GNU extensions.
Turning them off would be a useful portability check, and would be a step toward
building with MSVC.

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
