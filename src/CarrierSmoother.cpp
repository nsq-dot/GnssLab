#include "CarrierSmoother.h"

#include <cmath>

#include "Const.h"

namespace {

// "C1" pairs with "L1", "C7" with "L7". Anything that is not a code
// observation is left alone.
bool phaseTypeFor(const std::string &codeType, std::string &phaseType) {
    if (codeType.size() < 2 || codeType[0] != 'C') {
        return false;
    }
    phaseType = "L" + codeType.substr(1);
    return true;
}

} // namespace

void CarrierSmoother::smooth(ObsData &obsData, const std::set<SatID> *slipped) {

    for (auto &stv : obsData.satTypeValueData) {

        const SatID &sat = stv.first;
        auto &obs = stv.second;
        const bool satSlipped = (slipped != nullptr && slipped->count(sat) > 0);

        for (auto &ov : obs) {

            std::string phaseType;
            if (!phaseTypeFor(ov.first, phaseType)) {
                continue;
            }

            auto ph = obs.find(phaseType);
            if (ph == obs.end()) {
                continue;               // no carrier, nothing to smooth with
            }

            // Both are metres: RinexObsReader scales the carrier by its
            // wavelength on the way in, so the filter must not scale it again
            // (doing so made every code-minus-carrier step exceed the slip
            // threshold and reset the state on every epoch - the filter ran
            // and did nothing).
            const double code = ov.second;
            const double phase = ph->second;

            // Spelled out rather than concatenated: a StationID and a SatID
            // both stringify to things that could otherwise collide.
            const std::string key = obsData.station + "|" + sat.toString()
                                    + "|" + ov.first;
            State &s = states[key];

            bool fresh = !s.live || satSlipped;

            if (!fresh) {
                const double gap = obsData.epoch - s.last;
                if (gap > gapLimit || gap < 0.0) {
                    fresh = true;       // outage, or time ran backwards
                } else {
                    // Code and carrier must move together apart from the
                    // ionosphere's slow divergence, which at any single epoch
                    // is millimetres.
                    const double divergence = (phase - s.phase) - (code - s.code);
                    if (std::fabs(divergence) > slipThreshold) {
                        fresh = true;
                    }
                }
            }

            if (fresh) {
                s.code = code;
                s.n = 1;
                s.live = true;
            } else {
                if (s.n < window) {
                    ++s.n;
                }
                s.code += (code - s.code) / s.n
                          + (phase - s.phase) * (s.n - 1.0) / s.n;
            }

            s.phase = phase;
            s.last = obsData.epoch;

            ov.second = s.code;         // the solver only ever sees this
        }
    }
}
