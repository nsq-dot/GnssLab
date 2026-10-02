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

#include "CSDetector.h"

// 诊断默认关闭，并按 GnssFunc.cpp 的既有做法用 GNSSLAB_DEBUG_CSDET 打开。
// 这是逐历元逐卫星的打印，零基线那套 1 Hz 数据有约 8000 个历元。
// 只影响 stdout，不参与任何数值计算。
#ifndef GNSSLAB_DEBUG_CSDET
#define GNSSLAB_DEBUG_CSDET 0
#endif

VariableDataMap CSDetector::detect(ObsData &obsData) {

    // Each detector writes into its own map. They must not share one: they both
    // assign unconditionally, so running MW after GF would overwrite a GF
    // detection with MW's "no slip" and lose it.
    std::map<Variable, int> gfFlags;
    std::map<Variable, int> polyFlags;
    std::map<Variable, int> mwFlags;

    if (useGFdiff) {
        detectCSGFdiff(obsData, gfFlags,
                       satEpochGFData, satEpochDLData,
                       satEpochMeanDLData, satEpochSigmaDLData,
                       satEpochGFStatusData,
                       gfThreshold, deltaTMax);
    }

    if (useGFpoly) {
        detectCSGFpoly(obsData, polyFlags,
                       satEpochGFData, satEpochPolyPredData,
                       satEpochPolyResData, satEpochSigmaResData,
                       satEpochPolyStatusData,
                       polyWindow, gfThreshold, deltaTMax);
    }

    if (useMW) {
        detectCSMW(obsData, mwFlags,
                   satEpochMWData, satEpochMeanMWData,
                   satEpochMWStatusData);
    }

    // Union of the three, largest status wins. Only the truth of the result
    // matters downstream (see the note on the status encoding in the header),
    // so taking the maximum is enough to express "any detector saw something".
    VariableDataMap csFlagData;
    for (const std::map<Variable, int> *m : {&gfFlags, &polyFlags, &mwFlags}) {
        for (const auto &vd : *m) {
            const double flag = static_cast<double>(vd.second);
            auto it = csFlagData.find(vd.first);
            if (it == csFlagData.end() || flag > it->second) {
                csFlagData[vd.first] = flag;
            }
        }
    }

#if GNSSLAB_DEBUG_CSDET
    std::cout << "CSDetector::detect " << obsData.station
              << " epoch " << obsData.epoch
              << ": " << csFlagData.size() << " ambiguity flag(s), ";
    int nSet = 0;
    for (const auto &vd : csFlagData)
        if (vd.second != 0.0) nSet++;
    std::cout << nSet << " non-zero" << std::endl;
#endif

    return csFlagData;
};
