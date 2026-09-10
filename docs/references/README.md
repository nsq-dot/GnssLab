# References

Format specifications and background reading. The documents themselves are
**not** redistributed here — several are third-party copyrighted material. Links
point to the publishers, where most are freely available.

## Format specifications

| Document | Publisher |
|---|---|
| RINEX 2.11 | <https://files.igs.org/pub/data/format/rinex211.txt> |
| RINEX 3.04 | <https://files.igs.org/pub/data/format/rinex304.pdf> |
| RINEX 4.00 | <https://files.igs.org/pub/data/format/rinex_4.00.pdf> |
| SP3-d (precise orbits) | <https://files.igs.org/pub/data/format/sp3d.pdf> |
| IGS clock format (CLK) | <https://files.igs.org/pub/data/format/rinex_clock304.txt> |
| ICD-GPS-200C (GPS interface) | <https://www.gps.gov/technical/icdv/> |
| BeiDou ICD (BDS-SIS-ICD) | <http://en.beidou.gov.cn/SYSTEMS/ICD/> |

## Textbooks and background

- Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
  *GNSS Data Processing, Volume I: Fundamentals and Algorithms.* ESA
  Communications. — The reference for the models used here: broadcast ephemeris
  computation, Klobuchar and Hopfield/Saastamoinen corrections, and the standard
  single-point solution.
- Teunissen, P. J. G., & Montenbruck, O. (Eds.) (2017). *Springer Handbook of
  GNSS.* Springer. — Broader treatment, including the BeiDou specifics.
- Hofmann-Wellenhof, B., Lichtenegger, H., & Wasle, E. (2008). *GNSS — Global
  Navigation Satellite Systems: GPS, GLONASS, Galileo, and more.* Springer.

## Methods used in this code

| Topic | Where | Reference |
|---|---|---|
| Broadcast ephemeris → satellite position/clock | `src/NavEphGPS.cpp`, `src/NavEphBDS.cpp` | Sanz Subirana Vol. I, ch. 3; ICD-GPS-200C §20.3.3.4.3.3.1 |
| BeiDou GEO orbit handling | `src/NavEphBDS.cpp` | BDS-SIS-ICD; GEO satellites use a different rotation than IGSO/MEO |
| Klobuchar ionospheric model | `src/GnssFunc.cpp` | ICD-GPS-200C §20.3.3.5.2.5 |
| Hopfield / Saastamoinen troposphere | `src/GnssFunc.cpp` | Sanz Subirana Vol. I, ch. 5 |
| Weighted least squares | `src/SolverLSQ.cpp`, `src/SPPIFCode.cpp` | Any adjustment-theory text |
| Doppler velocity estimation | `src/SPPVelocity.cpp` | See below |
| Robust outlier rejection (MAD) | `src/SPPVelocity.cpp` | Huber-style scale estimate; `σ = 1.4826 · median|r|` |
| Melbourne–Wübbena cycle-slip detection | `apps/cs_detect_mw.cpp` | Melbourne (1985); Wübbena (1985) |

## On the Doppler velocity solution

Velocities are estimated from the Doppler observations directly, in a separate
weighted least-squares step from the position solution. The unknown is
`c·δṫ_r`, the receiver clock drift scaled by the speed of light, because that is
the quantity entering the range-rate equation. The class-level notes are in
`src/SPPVelocity.h`.

Rejection of bad observations uses a median-absolute-deviation scale rather than
the post-fit residual sigma. This matters: the residual sigma is itself inflated
by the outliers being rejected, so a threshold derived from it grows with the
contamination and can end up rejecting nothing. The MAD estimate is not
sensitive to a few large residuals, which is what makes it usable as a threshold
for finding them.
