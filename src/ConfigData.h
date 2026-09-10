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
#pragma once

#include <string>

struct SPPConfigData {

    std::string obsFile;
    std::string navFile;
    std::string outFile;

    bool GPS;
    bool BD2;
    bool BD3;
    bool Galileo;
    bool GLONASS;

    int cutOffElevation;
    int minSatNum;
    int maxGDOP;
    int tropModel;
    int ionoModel;

    int obsModel;

    double noiseGPSCode;
    double noiseBD2Code;
    double noiseBD3Code;

    int estimator;
};

// 声明全局变量
extern SPPConfigData sppConfigData;

