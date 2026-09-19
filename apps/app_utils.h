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
#include <vector>
#include <functional>
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

/**
 * One selectable constellation + frequency pair for the RTK solver.
 *
 * `rawTypes` are the three-character RINEX names to hand RinexObsReader;
 * `codePair` is the two-character pair (after convertObsType collapses "C1C" to
 * "C1") that SPPUCCodePhase derives its four observation types from - it builds
 * the phase types by swapping the leading C for an L, so C2/C7 implies L2/L7.
 *
 * ## Why BeiDou has two entries and neither is a superset
 *
 * The two BeiDou generations do not carry the same second frequency:
 *
 *   BDS-2 (C01..C16)   B1I + B2I    RINEX C2I + C7I    B2I = 1207.140 MHz
 *   BDS-3 (C19..)      B1I + B2a    RINEX C2I + C5P    B2a = 1176.450 MHz
 *
 * So B1I+B2I silently restricts the solution to BDS-2 satellites, and B1I+B2a
 * silently restricts it to BDS-3. Verified against data/Zero-baseline: the
 * BDS-3 satellites carry C1P and C5P and no C7I at all, and the BDS-2 ones
 * carry C7I and no C5P. On the zero-baseline set both choices yield six
 * satellites, but BDS-2's six are IGSO-dominated and clustered over the
 * Asia-Pacific, while BDS-3's six are MEOs - and the RTK result differs by
 * roughly 15x. Neither pair is "the right one"; the exercise is to compare.
 *
 * Note this table is deliberately NOT the one cycleSlipObsTypes() returns. The
 * chapter-7 detectors are locked to B1I/B2I because that is what they were
 * built and validated against; RTK is free to choose. Keeping the two tables
 * apart is the point - do not merge them.
 */
struct RtkMode {
    string key;                          // "gps" | "bds2" | "bds3"
    string system;                       // "G" or "C"
    string label;                        // human-readable, for --help and the manifest
    std::set<string> rawTypes;           // 3-char names to select in the RINEX reader
    std::pair<string, string> codePair;  // 2-char names used by SPPUCCodePhase::dualCodeTypes
};

/// Every mode the program accepts, in the order --help lists them.
inline std::vector<RtkMode> rtkModes() {
    std::vector<RtkMode> m;

    RtkMode gps;
    gps.key = "gps";
    gps.system = "G";
    gps.label = "GPS L1/L2 (C1C+C2W)";
    gps.rawTypes = {"C1C", "C2W", "L1C", "L2W"};
    gps.codePair = {"C1", "C2"};
    m.push_back(gps);

    RtkMode bds2;
    bds2.key = "bds2";
    bds2.system = "C";
    bds2.label = "BDS-2 B1I/B2I (C2I+C7I)";
    bds2.rawTypes = {"C2I", "C7I", "L2I", "L7I"};
    bds2.codePair = {"C2", "C7"};
    m.push_back(bds2);

    RtkMode bds3;
    bds3.key = "bds3";
    bds3.system = "C";
    bds3.label = "BDS-3 B1I/B2a (C2I+C5P)";
    bds3.rawTypes = {"C2I", "C5P", "L2I", "L5P"};
    bds3.codePair = {"C2", "C5"};
    m.push_back(bds3);

    return m;
}

/// Look up a mode by its key. Returns false when the key is not one of them.
inline bool findRtkMode(const string &key, RtkMode &out) {
    for (const RtkMode &m : rtkModes()) {
        if (m.key == key) {
            out = m;
            return true;
        }
    }
    return false;
}

/// All mode keys joined for an error message, e.g. "gps, bds2, bds3".
inline string rtkModeKeys() {
    string s;
    for (const RtkMode &m : rtkModes()) {
        if (!s.empty()) s += ", ";
        s += m.key;
    }
    return s;
}

#endif //GNSSLAB_APP_UTILS_H
