/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Author:
 *  Shoujian Zhang，shjzhang@sgg.whu.edu.cn， 2024-10-10
 *
 * References:
 *  1.  Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
 *      GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
 *  2.  Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
 */

#ifndef GNSSLAB_CSDETECTOR_H
#define GNSSLAB_CSDETECTOR_H
#include "GnssStruct.h"
#include "GnssFunc.h"

/**
 * Cycle-slip flags for the RTK filter, keyed by the ambiguity parameters the
 * Kalman solver carries across epochs.
 *
 * This is the producer half of the chapter-7 -> chapter-8 connection.
 * ``SolverKalman::solve(equSys, csData)`` reads a ``VariableDataMap`` keyed by
 * ``Parameter::ambiguity`` variables and, where an entry is non-zero, resets
 * that ambiguity instead of propagating it as a constant. Nothing in ``apps/``
 * produced one of those until now; the only thing that ever did was the
 * lecture notes' own Kalman program, ``examples/exam-8.5-rtk_kal.cpp``, which
 * was never built. So the file looked like dead code - it is not, it is the
 * missing producer.
 *
 * ## What it does now
 *
 * It no longer detects anything itself. It used to carry its own
 * Melbourne-Wubbena implementation, which was the same arithmetic as
 * ``detectCSMW()`` written out a second time, and it only handled GPS - the
 * ``else`` branch threw every non-GPS satellite away. Both are gone: the work is
 * delegated to the chapter-7 detectors, which support GPS and BeiDou, have
 * thresholds that were measured rather than picked, and are covered by
 * ``tests/test_cycle_slip_scoring.py``.
 *
 * ``detect()`` runs the detectors that are switched on and returns the
 * **union** of their flags, taking the largest status where they disagree.
 *
 * ## Why the union, and why these two
 *
 * GF and MW have complementary blind spots, which is the finding chapter 7
 * ends on: GF cannot see a slip of ``(77k, 60k)`` cycles and MW cannot see
 * ``(k, k)``. Neither is redundant.
 *
 * For a *filter* the two kinds of mistake are not symmetric. A missed slip
 * leaves a wrong ambiguity in the state, where it biases every following epoch
 * silently - the worst failure this layer can have. A false one resets an
 * ambiguity that was fine, which costs a few epochs of reconvergence and
 * nothing else. So the default is the union, which is deliberately the more
 * sensitive combination, and the polynomial detector is off by default because
 * chapter 7 measured it as the less sensitive of the three at 1 Hz.
 *
 * ## The status encoding, and what consumes it
 *
 * ``detectCSGFdiff`` reports ``CSGFStatus`` - ``OK``, ``SLIP``, ``INIT``,
 * ``GAP``, ``WARMUP`` - not a 0/1 flag. ``SolverKalman`` tests the entry for
 * truth, so every non-zero status resets the ambiguity, and that is **correct**:
 * an arc start, a data gap and an unfilled polynomial window all mean the
 * ambiguity is not the one the filter was carrying. It is also not an accident
 * worth relying on - the five-state encoding was added in chapter 7, after the
 * consumer was written. Do not "fix" it to ``== CSGF_SLIP``; a 1-only test would
 * propagate an ambiguity straight across a gap.
 */
class CSDetector {
public:

    CSDetector()
            : deltaTMax(120.0), gfThreshold(0.030), polyWindow(30),
              useGFdiff(true), useGFpoly(false), useMW(true) {};

    /**
     * Slip flags for one epoch, keyed by ambiguity variable.
     *
     * Non-zero means "reset this ambiguity". The value is the largest status
     * any switched-on detector reported for that variable, so it is either a
     * ``CSGFStatus`` or MW's 0/1.
     *
     * **This modifies `obsData`**: the chapter-7 detectors drop the satellites
     * they cannot form a combination for, and that is left as it is. Call it
     * after the single-point solve, not before - ``exam-8.5-rtk_kal.cpp`` does
     * exactly that, which is why the erasure has never caused trouble.
     */
    VariableDataMap detect(ObsData &obsData);

    ~CSDetector(){};

    //-----------------------------------------------------------------
    // Which detectors, and how they are tuned. The defaults reproduce the
    // chapter-7 command-line programs' own defaults.
    //-----------------------------------------------------------------
    /// Data gap beyond which no judgement is made [s].
    double deltaTMax;
    /// GF epoch-difference threshold [m]. 0.030 = 0.56*|lambda1-lambda2|.
    double gfThreshold;
    /// Polynomial fit window [epochs].
    int polyWindow;

    bool useGFdiff;
    bool useGFpoly;
    bool useMW;

    //-----------------------------------------------------------------
    // Per-epoch series, kept so a caller can inspect what the detectors saw.
    // The chapter-7 detectors require somewhere to put these; they are the same
    // maps cs_detect_gf / cs_detect_mw write to file.
    //-----------------------------------------------------------------
    SatEpochValueMap satEpochGFData;
    SatEpochValueMap satEpochDLData;
    SatEpochValueMap satEpochMeanDLData;
    SatEpochValueMap satEpochSigmaDLData;
    SatEpochValueMap satEpochGFStatusData;

    SatEpochValueMap satEpochPolyPredData;
    SatEpochValueMap satEpochPolyResData;
    SatEpochValueMap satEpochSigmaResData;
    SatEpochValueMap satEpochPolyStatusData;

    SatEpochValueMap satEpochMWData;
    SatEpochValueMap satEpochMeanMWData;
    SatEpochValueMap satEpochMWStatusData;
};


#endif //GNSSLAB_CSDETECTOR_H
