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

class CSDetector {
public:

    CSDetector()
    : deltaTMax(120.0), minCycles(2.0)
    {};

    VariableDataMap detect(ObsData &obsData);

    ~CSDetector(){};

    SatEpochValueMap satEpochMWData;
    SatEpochValueMap satEpochMeanMWData;
    SatEpochValueMap satEpochCSFlagData;

    // A structure used to store filter data for a SV.
    struct MWData {
        // Default constructor initializing the data in the structure
        MWData()
                : formerEpoch(BEGINNING_OF_TIME), windowSize(0), meanMW(0.0), varMW(0.0) {};

        CommonTime formerEpoch; ///< The previous epoch time stamp.
        int windowSize;         ///< Size of current window, in samples.
        double meanMW;          ///< Accumulated mean value of combination.
        double varMW;           ///< Accumulated std value of combination.
    };

    std::map<SatID, MWData> satMWData;


    double deltaTMax;
    double minCycles;

};


#endif //GNSSLAB_CSDETECTOR_H
