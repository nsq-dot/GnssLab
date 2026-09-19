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

    void setDualCodeTypes(std::map<string, std::pair<string, string>>& types)
    {
        dualCodeTypes = types;
    };

    std::map<string, std::pair<string, string>> dualCodeTypes;

};


#endif //GNSSLAB_SPPUCCODEPHASE_H
