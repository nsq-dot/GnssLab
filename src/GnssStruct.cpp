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
#include "GnssStruct.h"

//====
// Variable
//====

// 初始化静态成员
const string Parameter::paraNameStrings[] = {
        "Unknown", "dX", "dY", "dZ", "cdt", "ifb", "iono", "ambiguity"
};

bool Variable::operator<(const Variable &right) const {
    if (station == right.station) {
        if (paraName == right.paraName) {
            if (obsID == right.obsID) {
                return (sat < right.sat);
            } else {
                return (obsID < right.obsID);
            }
        } else {
            return (paraName < right.paraName);
        }
    } else {
        return (station < right.station);
    }
}

bool Variable::operator==(const Variable &right) {
    if (station == right.station &&
        sat == right.sat &&
        obsID == right.obsID &&
        paraName == right.paraName) {
        return true;
    } else {
        return false;
    }
}

bool Variable::operator!=(const Variable &right) {
    return (!((*this) == right));
}



