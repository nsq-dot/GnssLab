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
// 必须与 GnssStruct.h 的 ParameterName 逐项对应、个数一致：toString() 按下标取，
// toParameterName() 按下标回指。此前这张表只有 8 项而枚举有 14 项，于是 cdtBDS
// 之后的每一个参数取名字都会越界读（测速与 ifb/iono/ambiguity 都受影响）。
const string Parameter::paraNameStrings[] = {
        "Unknown", "dX", "dY", "dZ", "cdt", "cdtBDS",
        "dVX", "dVY", "dVZ", "cdtDot", "cdtDotBDS",
        "ifb", "iono", "ambiguity", "trop"
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



