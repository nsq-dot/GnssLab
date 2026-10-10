#ifndef GNSSLAB_CARRIERSMOOTHER_H
#define GNSSLAB_CARRIERSMOOTHER_H

#include <map>
#include <set>
#include <string>

#include "GnssStruct.h"

// Hatch carrier smoothing of the code observations.
//
// Why this exists: on the chapter's zero-baseline data the single-point
// solution sits ~20 m from the receivers' own reported positions, and the RTK
// column is perfect at the same time. Every one of those 20 m is common to the
// two stations, so the double difference cancels it - and the one thing a
// receiver does that this solver did not is filter its code with its carrier.
// Code is noisy and slowly multipath-biased; the carrier is neither. Smoothing
// is what closes that gap, and it is the only mechanism left that explains both
// the offset and the ±10 m slow wander the SPP shows even at a good site.
//
// The filter is Hatch (1982), the textbook one:
//
//     P_s(k) = P_s(k-1) + [P(k) - P_s(k-1)] / N(k)
//                       + [L(k) - L(k-1)] * (N(k) - 1) / N(k)
//     N(k)   = min(N(k-1) + 1, window)
//
// State is per (station, satellite, code type); the carrier used is the phase
// of the same band. It resets on the first epoch, on a data gap longer than
// gapLimit, on a code-minus-carrier divergence larger than slipThreshold (the
// classic slip test for this filter - it catches gross slips, not every one;
// pass `slipped` to reset exactly where chapter 7's detectors fired), and on
// request via reset().
//
// Output: the code values in the ObsData are rewritten in place. Phase values
// are read, never written. Off unless a caller turns it on, because it moves
// the code observations and the chapter's byte-exact anchor was produced
// without it.
class CarrierSmoother {
public:
    // Window in epochs. 100 is the conventional default; a longer window
    // smooths harder but holds a slip's bias longer.
    void setWindow(int n) { window = (n > 0) ? n : 1; }

    // Data gap that resets the filter [s]. 120 s covers both the 1 Hz and the
    // 30 s datasets here without firing on their normal cadence.
    void setGapLimit(double seconds) { gapLimit = seconds; }

    // Code-minus-carrier step that counts as a slip [m].
    //
    // This is a BACKSTOP, not the reset signal. The real one is chapter 7's
    // cycle-slip flags, passed to smooth(): the divergence test cannot tell a
    // one-cycle slip (0.2 m) from noise, so a threshold tight enough to catch
    // slips fires constantly on slow code multipath - which is exactly what
    // smoothing is supposed to remove. Measured on the 2025 Wuhan day (24 h,
    // precise products): a 2 m threshold left the 3-D RMS at 4.94 m, i.e. no
    // gain at all, while never resetting at all carried real slips into the
    // code and made it 9.77 m. Hence a loose threshold here and the flags
    // there.
    void setSlipThreshold(double metres) { slipThreshold = metres; }

    // Rewrite the code observations in place. `slipped`, when given, names
    // satellites whose carrier is discontinuous this epoch (chapter 7's flags).
    void smooth(ObsData &obsData, const std::set<SatID> *slipped = nullptr);

    // Forget every filter state (a new run, or a deliberate restart).
    void reset() { states.clear(); }

private:
    struct State {
        double code = 0.0;      // smoothed code, metres
        double phase = 0.0;     // last carrier, metres
        int n = 0;              // epochs in the window
        bool live = false;
        CommonTime last;
    };

    std::map<std::string, State> states;
    int window = 100;
    double gapLimit = 120.0;
    // Loose on purpose - see setSlipThreshold: the flags do the work, this
    // only stops the filter dead on a gross discontinuity.
    double slipThreshold = 15.0;
};

#endif // GNSSLAB_CARRIERSMOOTHER_H
