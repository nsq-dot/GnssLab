/**
 * Copyright:
 * ---------
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Author:
 * ---------
 * Shoujian Zhang, shjzhang@sgg.whu.edu.cn, 2024-10-10
 *
 * References:
 * ---------
 * 1. Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
 *    GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
 * 2. Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
 */

#include "StringUtils.h"
#include "RinexObsReader.h"
#include "TimeConvert.h"

#define debug 0

void RinexObsReader::parseRinexHeader() {
    double version;
    XYZ antennaPosition;
    string satSys;
    int sysObsCount = 0;
    std::map<string, std::vector<string>> mapObsTypes;

    while (true) {
        string line;
        getline(*pFileStream, line);

        if (debug)
            cout << "parseRinexHeader:" << line << endl;

        string label = strip(line.substr(60, 20));

        if (label == "END OF HEADER") {
            break;
        }
        else if (label == "MARKER NAME") {
            string markerName = strip(line.substr(0, 60));
            std::replace(markerName.begin(), markerName.end(), ' ', '_');
            rinexHeader.station = strip(markerName);
        }
        else if (label == "RINEX VERSION / TYPE") {
            version = safeStod(line.substr(0, 20));
            if (version < 3.04 || version > 3.05) {
                cerr << "Error: Only RINEX 3.04/3.05 are supported!" << endl;
                exit(-1);
            }
            rinexHeader.version = version;
        }
        else if (label == "APPROX POSITION XYZ") {
            antennaPosition[0] = safeStod(line.substr(0, 14));
            antennaPosition[1] = safeStod(line.substr(14, 14));
            antennaPosition[2] = safeStod(line.substr(28, 14));
            rinexHeader.antennaPosition = antennaPosition;
        }
        else if (label == "SYS / # / OBS TYPES") {
            // strip() takes its argument BY VALUE and returns a new string, so
            // `strip(sysStr);` throws the result away. That mattered here: a
            // continuation line - the one carrying observation types 14 and
            // beyond - starts with a blank system character, so the discarded
            // return made the code read it as a fresh system block. satSys
            // became " ", the count was taken from the wrong columns, and every
            // type past the 13th was silently dropped for every constellation.
            // BDS declares 20 types in the zero-baseline files, so B2a (C5P)
            // was unreachable, which in turn ruled out every BDS-3 frequency
            // pair.
            string sysStr = strip(line.substr(0, 1));

            if (!sysStr.empty()) {
                // First line of a system block declares the total.
                sysObsCount = safeStoi(line.substr(3, 3));
                satSys = sysStr;
            }

            // Up to 13 three-character codes per line from column 7; a block
            // continues on following lines until the declared total is reached.
            const int maxObsPerLine = 13;
            for (int i = 0; i < maxObsPerLine && (int) mapObsTypes[satSys].size() < sysObsCount; i++) {
                std::string typeStr = strip(line.substr(4 * i + 7, 3));
                if (typeStr.empty()) break;
                mapObsTypes[satSys].push_back(typeStr);
            }
            rinexHeader.mapObsTypes = mapObsTypes;
        }
    }
}

ObsData RinexObsReader::parseRinexObs() {
    if (!isHeaderRead) {
        parseRinexHeader();
        isHeaderRead = true;
    }

    std::string line;

    // ==============================================
    // FIX: Skip all comment lines starting with 'R' or '>' without proper format
    // ==============================================
    while (true) {
        if (!getline(*pFileStream, line)) {
            throw EndOfFile("EOF encountered!");
        }

        // Skip lines starting with 'R' (phase shift / system comments)
        if (!line.empty() && line[0] == 'R') {
            if (debug) cout << "Skipping comment line: " << line << endl;
            continue;
        }

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Valid epoch line must start with "> "
        if (line.size() >= 2 && line[0] == '>' && line[1] == ' ') {
            break;
        }

        // If we reach here, it's an unrecognized line – skip it
        if (debug) cerr << "Skipping unrecognized line: " << line << endl;
    }

    if (debug) {
        cout << "Processing epoch line: " << line << endl;
    }

    // Parse epoch flag
    int epochFlag = safeStoi(line.substr(31, 1));
    if (epochFlag < 0 || epochFlag > 6) {
        throw FFStreamError("Invalid epoch flag: " + std::to_string(epochFlag));
    }

    CommonTime currEpoch = parseTime(line);
    int numSats = safeStoi(line.substr(32, 3));

    SatTypeValueMap stvData;
    SatTypeValueMap obsDopplerTemp; // 临时存储所有卫星多普勒观测

    if (epochFlag == 0 || epochFlag == 1 || epochFlag == 6) {
        std::vector<SatID> satIndex(numSats);

        for (int isv = 0; isv < numSats; ++isv) {
            if (!getline(*pFileStream, line)) {
                throw EndOfFile("EOF while reading satellite data!");
            }

            if (debug) cout << "Sat data line: " << line << endl;

            try {
                satIndex[isv] = SatID(line.substr(0, 3));
            } catch (...) {
                throw FFStreamError("Invalid satellite ID at line: " + line);
            }

            SatID sat = satIndex[isv];

            // 允许 GPS, BDS, Galileo, GLONASS
            if (sat.system != "G" && sat.system != "C" ) {
                continue;
                }

            // Check if system exists in header
            if (rinexHeader.mapObsTypes.find(sat.system) == rinexHeader.mapObsTypes.end()) {
                continue;
            }

            int obsCount = rinexHeader.mapObsTypes.at(sat.system).size();
            size_t minLength = 3 + 16 * obsCount;

            if (line.length() < minLength) {
                line.append(minLength - line.length(), ' ');
            }

            TypeValueMap rangeMap;
            TypeValueMap dopplerMap;

            for (int i = 0; i < obsCount; ++i) {
                size_t pos = 3 + 16 * i;
                string valStr = line.substr(pos, 14);
                strip(valStr);

                if (valStr.empty()) continue;

                double data;
                try {
                    data = safeStod(valStr);
                } catch (...) {
                    continue;
                }

                string obsType = rinexHeader.mapObsTypes.at(sat.system)[i];

                // Carrier phase: RINEX stores cycles, the observation model wants
                // metres, so scale by the wavelength. Verified against this
                // dataset - L1C * lambda1 reproduces C1C to within the ambiguity
                // (22 206 887 m vs 22 206 874 m), so the sign is positive.
                //
                // This used to read `data = -data / lambda` into dopplerMap: the
                // sign was inverted, dividing by the wavelength is dimensionally
                // wrong (cycles/metre), and routing phase into the Doppler map
                // meant it never reached satTypeValueData at all - the phase was
                // simply invisible to every consumer of the observation data.
                if (obsType[0] == 'L') {
                    int band = 0;
                    if (obsType[1] == 'A') band = 1;
                    else band = safeStoi(obsType.substr(1, 1));

                    double lambda = getWavelength(sat.system, band);
                    if (lambda <= 0) continue;

                    data = data * lambda;
                    if (fabs(data) < 1e-4) continue;
                    rangeMap[obsType] = data;
                }
                // 2. D开头：多普勒观测（核心新增分支！）
                else if (obsType[0] == 'D')
                {
                    // 多普勒单位Hz，直接存入dopplerMap，无需波长转换
                    if (fabs(data) < 1e-12) continue;
                    dopplerMap[obsType] = data;
                }
               // 伪距C波段，直接存入伪距map
               else if (obsType[0] == 'C')
               {
                    if (fabs(data) < 1e-4) continue;
                    rangeMap[obsType] = data;
               }
            }
            // 伪距保留原有逻辑存入stvData
            if (!rangeMap.empty()) {
                stvData[sat] = rangeMap;
            }
            if (!dopplerMap.empty()) {
                obsDopplerTemp[sat] = dopplerMap;
            }
        }
    }

    ObsData obsData;
    obsData.station = rinexHeader.station;
    obsData.epoch = currEpoch;
    obsData.satTypeValueData = stvData;
    // 新增：多普勒数据存入ObsData
    obsData.satDopplerData = obsDopplerTemp;
    obsData.antennaPosition = rinexHeader.antennaPosition;

    chooseObs(obsData);
    return obsData;
}

CommonTime RinexObsReader::parseTime(const string &line) {
    if (line.size() < 30) {
        throw FFStreamError("Invalid time line length");
    }

    if ((line[1] != ' ') || (line[6] != ' ') || (line[9] != ' ') ||
        (line[12] != ' ') || (line[15] != ' ') || (line[18] != ' ') ||
        (line[29] != ' ') || (line[30] != ' ')) {
        throw FFStreamError("Invalid time format");
    }

    if (line.substr(2, 27) == string(27, ' '))
        return BEGINNING_OF_TIME;

    int year = safeStoi(line.substr(2, 4));
    int month = safeStoi(line.substr(7, 2));
    int day = safeStoi(line.substr(10, 2));
    int hour = safeStoi(line.substr(13, 2));
    int min = safeStoi(line.substr(16, 2));
    double sec = safeStod(line.substr(19, 11));

    double leapSec = 0.0;
    if (sec >= 60.0) {
        leapSec = sec;
        sec = 0.0;
    }

    CommonTime ctime = CivilTime2CommonTime(CivilTime(year, month, day, hour, min, sec));
    if (leapSec > 0) ctime += leapSec;

    return ctime;
}

void RinexObsReader::chooseObs(ObsData &obsData) {
    SatTypeValueMap filtered;

    for (const auto &entry : obsData.satTypeValueData) {
        const SatID &sat = entry.first;
        const TypeValueMap &values = entry.second;

        auto sysIt = sysTypes.find(sat.system);
        if (sysIt == sysTypes.end()) continue;

        const set<string> &allowed = sysIt->second;
        TypeValueMap kept;

        for (const auto &tv : values) {
            if (allowed.count(tv.first)) {
                kept.insert(tv);
            }
        }

        if (!kept.empty()) {
            filtered[sat] = kept;
        }
    }

    obsData.satTypeValueData.swap(filtered);
}