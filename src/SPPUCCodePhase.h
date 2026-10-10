/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Author: Shoujian Zhang，shjzhang@sgg.whu.edu.cn， 2024-10-10
 *
 * References:
 * 1. Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
 *    GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
 * 2. Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
 */

#ifndef GNSSLAB_SPPUCCODEPHASE_H
#define GNSSLAB_SPPUCCODEPHASE_H
#include "SPPIFCode.h"

class SPPUCCodePhase: public SPPIFCode {
public:
    SPPUCCodePhase()
    {};

    EquSys linearize(Eigen::Vector3d& xyz,
                     std::map<SatID,Xvt>& satXvtRecTime,
                     SatValueMap& satElevData,
                     ObsData& obsData);

    void solve(ObsData &obsData);

    void checkDualCodeTypes(ObsData &obsData);

    void setDualCodeTypes(std::map<string, std::vector<std::pair<string, string>>>& types)
    {
        dualCodeTypes = types;
    };

    /**
     * Estimate a receiver inter-system bias between the two BeiDou generations.
     *
     * Off by default, which leaves every existing mode exactly as it was. On, a
     * single extra unknown is added to every observation equation of a BDS-3
     * satellite (and none of a BDS-2 one); after the station and satellite
     * differences it survives only where a double difference spans the two
     * generations, which is precisely the case where the receiver bias fails to
     * cancel. See the note in apps/rtk.cpp's --isb.
     *
     * Deliberately a plain flag rather than a set of satellites: what makes the
     * two groups differ is their generation, and that is a property of the
     * satellite, not of this solver.
     */
    void setEstimateISB(bool on) { estimateISB = on; };
    bool estimateISB = false;

    /**
     * Estimate the residual zenith tropospheric delay as an unknown.
     *
     * Off, the modelled slant delay (Saastamoinen zenith, Niell dry mapping,
     * 1/(sinE + 0.00095) wet) is taken as true, and every bit of its error
     * lands on the station's height. A ZHD/ZWD pair computed from standard
     * meteorology is routinely 5-20 cm of zenith delay away from the real one,
     * which the geometry maps straight into the vertical.
     *
     * On, one more station-level unknown is added, the derivative being the wet
     * mapping function - the same one the model's wet term uses, so the unknown
     * is the correction to the model and not a second, competing model. It is
     * per epoch, because a single-epoch solution has nothing else to link it
     * across time.
     *
     * The clock and this parameter are close to collinear (the clock's
     * coefficient is a constant 1, this one varies with elevation), so what
     * separates them is the spread of elevations in view. That is exactly the
     * classical geometry of the problem, and it is why the estimate is noisier
     * than the model - it trades a bias for variance.
     *
     * MEASURED, and the reason this is off by default: at a single epoch that
     * trade is not worth making. On the 2025 Wuhan day (2805 epochs, truth
     * known) modelling the delay gives 1.91 m mean and 8.68 m 3-D RMS, while
     * estimating it per epoch gives 3.37 m and 29.70 m - the height goes wild
     * at epochs whose satellites are bunched in elevation, because the clock
     * absorbs exactly the part of the delay the geometry cannot separate. The
     * classical answer is a *constrained* estimate carried across epochs: an a
     * priori sigma plus a random walk, i.e. a filter state, which is what
     * SolverKalman is for. This flag is the per-epoch version, kept because it
     * demonstrates that, and because it is the piece the filter would reuse.
     */
    void setEstimateTrop(bool on) { estimateTrop = on; };
    bool estimateTrop = false;

    /**
     * Acceptable two-character code pairs per system, tried in order.
     *
     * One entry is the ordinary case: every satellite of that system must carry
     * both codes. More than one entry exists because a mixed BDS-2 + BDS-3
     * solution has no single pair that covers both generations - they broadcast
     * different second frequencies - so a satellite takes the first pair it can
     * satisfy in full.
     */
    std::map<string, std::vector<std::pair<string, string>>> dualCodeTypes;

};


#endif //GNSSLAB_SPPUCCODEPHASE_H
