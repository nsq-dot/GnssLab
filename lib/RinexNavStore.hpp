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

#ifndef RinexNavStore_HPP
#define RinexNavStore_HPP

#include <iostream>
#include <string>
#include <list>
#include <map>
#include <algorithm>
#include <fstream>

#include "NavEphGPS.hpp"
#include "GnssStruct.h"
#include "NavEphBDS.hpp"

using namespace std;

class RinexNavStore {
public:

    RinexNavStore() {};

    void loadGPSEph(NavEphGPS &gpsEph, string &line, fstream &navFile);
    void loadBDSEph(NavEphBDS &bdsEph, string &line, fstream &navFile);
    void loadFile(string &file);
    Xvt getXvt(const SatID &sat, const CommonTime &epoch);
    NavEphGPS findGPSEph(const SatID &sat, const CommonTime &epoch);
    NavEphBDS findBDSEph(const SatID &sat, const CommonTime &epoch);
    bool ifBDS_GEO(const SatID& sat) const;
    /// destructor
    virtual ~RinexNavStore() {};

    struct TimeSysCorr {
        double A0;
        double A1;
        int refSOW;
        int refWeek;
        string geoProvider;
        int geoUTCid;
    };

    typedef map<string, vector<double>> ionoCorrMap;
    typedef map<string, TimeSysCorr> timeSysCorrMap;

    string rx3NavFile;
    double version;                ///< RINEX Version
    std::string fileType;          ///< File type "N...."
    std::string fileSys;           ///< File system string
    std::string fileProgram;       ///< Program string
    std::string fileAgency;        ///< Agency string
    std::string date;              ///< Date string; includes "UTC" at the end
    std::vector<std::string> commentList;  ///< Comment list

    ionoCorrMap ionoCorrData;
    timeSysCorrMap timeSysCorrData;

    long leapSeconds;              ///< Leap seconds
    long leapDelta;                ///< Change in Leap seconds at ref time
    long leapWeek;                 ///< Week number of ref time
    long leapDay;                  ///< Day of week of ref time

    static const std::string stringVersion;      /// "RINEX VERSION / TYPE"
    static const std::string stringRunBy;        /// "PGM / RUN BY / DATE"
    static const std::string stringComment;      /// "COMMENT"
    // R3.x
    static const std::string stringIonoCorr;     /// "IONOSPHERIC CORR"
    static const std::string stringTimeSysCorr;  /// "TIME SYSTEM CORR"
    static const std::string stringLeapSeconds;  /// "LEAP SECONDS"
    static const std::string stringDeltaUTC;     /// "DELTA-UTC: A0,A1,T,W" // R2.11 GPS
    static const std::string stringCorrSysTime;  /// "CORR TO SYSTEM TIME"  // R2.10 GLO
    static const std::string stringDUTC;         /// "D-UTC A0,A1,T,W,S,U"  // R2.11 GEO
    static const std::string stringIonAlpha;     /// "ION ALPHA"            // R2.11
    static const std::string stringIonBeta;      /// "ION BETA"             // R2.11
    static const std::string stringEoH;          /// "END OF HEADER"

    ///tables that record the sat of different sat system
    ///in order to get the eph data num easily
    vector<SatID> satTable;

    map<SatID, std::map<CommonTime, NavEphGPS>> gpsEphData;
    map<SatID, std::map<CommonTime, NavEphBDS>> bdsEphData;

    void printAllGPSNav()
    {
        cout << endl;
        cout << "=====================================" << endl;
        cout << "    GPS Navigation Ephemeris" << endl;
        cout << "=====================================" << endl;

        for (auto& satEntry : gpsEphData) {
            SatID sat = satEntry.first;
            NavEphGPS eph = satEntry.second.begin()->second;

            cout << "Satellite: " << sat << endl;
            cout << "  Week    : " << eph.GPSWeek << endl;
            cout << "  Toe     : " << eph.Toe << endl;
            cout << "  af0     : " << eph.af0 << endl;
            cout << "  af1     : " << eph.af1 << endl;
            cout << "  e       : " << eph.ecc << endl;
            cout << "  A       : " << eph.sqrt_A * eph.sqrt_A << endl;
            cout << "-------------------------------------" << endl;
        }

        // 统计卫星数量
        int totalSatCount = gpsEphData.size();
        // 先打印总数
        cout << endl;
        cout << "Total satellites: " << totalSatCount << endl;
        cout << "-------------------------------------" << endl;
    }

};



#endif // RinexNavStore_HPP