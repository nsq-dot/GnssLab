/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Small helpers shared by the command-line programs under apps/.
 *
 * These deliberately duplicate the private helpers at the top of
 * apps/spp_if.cpp rather than replacing them. spp_if is the program the frozen
 * regression baseline in tests/baseline/ was produced by, and rewriting it to
 * prove a point about DRY would put those byte-for-byte comparisons at risk for
 * no functional gain. New programs include this header; spp_if keeps its own.
 */

#ifndef GNSSLAB_APP_UTILS_H
#define GNSSLAB_APP_UTILS_H

#include <string>
#include <fstream>
#include <iostream>
#include <set>
#include <map>
#include <filesystem>
#include <system_error>

using std::string;

/// Directory part of a path, or "." when there is none.
inline string dirOf(const string &path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

/// Filename part of a path, extension included.
inline string fileNameOf(const string &path) {
    size_t slash = path.find_last_of("/\\");
    return (slash == string::npos) ? path : path.substr(slash + 1);
}

/// Parse "YYYY-MM-DDTHH:MM:SS" (a space instead of 'T' also works).
inline bool parseISO8601(const string &s, int &y, int &mo, int &d, int &h, int &mi, double &sec) {
    if (s.size() < 19) return false;
    string t = s;
    t[10] = ' ';
    if (t[13] != ':' || t[16] != ':') return false;
    try {
        y = stoi(t.substr(0, 4));
        mo = stoi(t.substr(5, 2));
        d = stoi(t.substr(8, 2));
        h = stoi(t.substr(11, 2));
        mi = stoi(t.substr(14, 2));
        sec = stod(t.substr(17));
    } catch (const std::exception &) {
        return false;
    }
    return true;
}

/// Create a directory, and any missing parents, if it does not already exist.
///
/// Uses <filesystem> rather than shelling out to `mkdir -p`: that spelling is
/// not portable to cmd.exe, and going through system() would mean quoting a path
/// that may contain spaces.
inline bool ensureDirectory(const string &dir) {
    if (dir.empty() || dir == ".") return true;

    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) return true;

    std::filesystem::create_directories(dir, ec);
    // create_directories reports failure if the directory appeared concurrently,
    // so confirm the end state rather than trusting the error code alone.
    return std::filesystem::is_directory(dir, ec);
}

/// True if the path exists and is a regular file.
inline bool fileExists(const string &path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

/**
 * Observation types to keep, per constellation, for cycle-slip detection.
 *
 * After convertObsType() collapses "L1C" to "L1", the detectors look for the
 * two-character names. The codes below are the ones that collapse onto the
 * frequencies the MW and GF combinations are defined on:
 *
 *   GPS   L1/L2  and  C1/C2   from  L1C/L2W and C1C/C2W
 *   BDS   B1I/B2I, which RINEX 3 spells L2/L7 and C2/C7
 *
 * BDS is the trap here: B1I/B2I is band 2/7, not band 2/6. Selecting C6I/L6I
 * (band 6, B3I) leaves L7/C7 absent, and the detector - which needs both -
 * drops every BeiDou satellite without a word.
 *
 * Both shipped datasets carry all of these types, so one table serves both.
 */
inline std::map<string, std::set<string>> cycleSlipObsTypes(bool gps, bool bds) {
    std::map<string, std::set<string>> sysTypes;

    if (gps) {
        sysTypes["G"].insert("C1C");
        sysTypes["G"].insert("C2W");
        sysTypes["G"].insert("L1C");
        sysTypes["G"].insert("L2W");
    }

    if (bds) {
        sysTypes["C"].insert("C2I");
        sysTypes["C"].insert("C7I");
        sysTypes["C"].insert("L2I");
        sysTypes["C"].insert("L7I");
    }

    return sysTypes;
}

#endif //GNSSLAB_APP_UTILS_H
