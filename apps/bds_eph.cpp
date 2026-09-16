/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Broadcast-versus-precise ephemeris for a single satellite at a single epoch:
 * orbit, velocity and clock bias from the broadcast ephemeris against the MGEX
 * precise orbit, with the differences. The program that sweeps a whole
 * constellation over a time series is apps/bds_gps_diff.
 *
 * Everything goes to stdout; this program writes no files, and so it
 * deliberately has no --out-dir option.
 *
 * Usage:
 *   bds_eph [config.ini] [options]
 *
 *   config.ini            Configuration file (default: config/eph.ini).
 *                         Relative paths inside it resolve against the config
 *                         file's own directory, not the working directory.
 *                         When the default is not in the working directory the
 *                         parent directories are searched for it. A config named
 *                         explicitly on the command line is not searched for.
 *
 * Options (override the config file):
 *   --nav <file>          RINEX broadcast navigation file
 *   --sp3 <file>          MGEX precise orbit (SP3)
 *   --sat <id>            Satellite to compare, e.g. C01
 *   --epoch <ISO8601>     The epoch to evaluate, e.g. 2025-01-01T00:05:00
 *   --verbose             Echo the resolved settings before the report
 *   -h, --help            This message
 *
 * Note that --epoch is not a stop epoch: it names the single epoch to evaluate,
 * the counterpart of bds_gps_diff's --start.
 */

#include "TimeStruct.h"
#include "TimeConvert.h"
#include "GnssStruct.h"
#include "RinexNavStore.hpp"
#include "SP3Store.hpp"
#include "ConfigData.h"
#include "app_utils.h"

#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

namespace {

void printUsage(const char *prog) {
    cout <<
         "Usage: " << prog << " [config.ini] [options]\n"
         "\n"
         "Broadcast versus precise ephemeris, one satellite at one epoch.\n"
         "\n"
         "  config.ini         Configuration file (default: config/eph.ini)\n"
         "\n"
         "Options:\n"
         "  --nav <file>       RINEX broadcast navigation file\n"
         "  --sp3 <file>       MGEX precise orbit (SP3)\n"
         "  --sat <id>         Satellite to compare, e.g. C01\n"
         "  --epoch <ISO8601>  The epoch to evaluate, e.g. 2025-01-01T00:05:00\n"
         "  --verbose          Echo the resolved settings\n"
         "  -h, --help         This message\n";
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/eph.ini";
    bool haveConfigArg = false;

    string optNav, optSp3, optSat, optEpoch;
    bool optVerbose = false;

    for (int i = 1; i < argc; ++i) {
        string a = argv[i];

        auto needValue = [&](const char *name) -> string {
            if (i + 1 >= argc) {
                cerr << "Error: " << name << " requires a value\n";
                exit(2);
            }
            return argv[++i];
        };

        if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else if (a == "--nav")     optNav = needValue("--nav");
        else if (a == "--sp3")     optSp3 = needValue("--sp3");
        else if (a == "--sat")     optSat = needValue("--sat");
        else if (a == "--epoch")   optEpoch = needValue("--epoch");
        else if (a == "--verbose") optVerbose = true;
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n";
            printUsage(argv[0]);
            return 2;
        } else if (!haveConfigArg) {
            configFile = a;
            haveConfigArg = true;
        } else {
            cerr << "Error: unexpected argument '" << a << "'\n";
            return 2;
        }
    }

    //---------------------------------------------------------------
    // Configuration
    //---------------------------------------------------------------
    EphConfigData cfg = EphConfigData::defaults();
    string configDir;

    // See apps/spp_if.cpp for why the default name is searched upwards. Only the
    // default is: a file the user named is taken at its word.
    if (!haveConfigArg && !fileExists(configFile)) {
        string found = findConfigUpwards(configFile);
        if (!found.empty()) {
            configFile = found;
            cerr << "Note: using config found by searching upwards: " << configFile << "\n";
        }
    }

    if (!fileExists(configFile)) {
        if (haveConfigArg) {
            cerr << "Error: cannot open config file: " << configFile << "\n";
            return 1;
        }
        cerr << "Note: " << configFile << " not found; using built-in defaults.\n"
             << "      Relative paths then resolve against the working directory ("
             << std::filesystem::current_path().string() << ").\n";
    } else {
        try {
            cfg = EphConfigData::fromIni(configFile);
            configDir = dirOf(configFile);
        } catch (const std::exception &e) {
            cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }

    // Command-line overrides win over the config file.
    if (!optNav.empty())   cfg.navFile = optNav;
    if (!optSp3.empty())   cfg.sp3File = optSp3;
    if (!optSat.empty())   cfg.targetSat = optSat;
    if (!optEpoch.empty()) cfg.targetUTC = optEpoch;

    // Relative config paths resolve against the project root; an explicit
    // command-line path is used verbatim. See apps/spp_if.cpp for the rule.
    string projectRoot = configDir.empty() ? "." : dirOf(configDir);

    // Named locals, not temporaries: RinexNavStore::loadFile takes a non-const
    // reference.
    string navFile = optNav.empty() ? resolvePath(projectRoot, cfg.navFile) : cfg.navFile;
    string sp3File = optSp3.empty() ? resolvePath(projectRoot, cfg.sp3File) : cfg.sp3File;

    if (optVerbose) {
        cout << "config    : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "nav       : " << navFile << "\n";
        cout << "sp3       : " << sp3File << "\n";
        cout << "sat       : " << cfg.targetSat << "\n";
        cout << "epoch     : " << cfg.targetUTC << "\n";
    }

    // ==========================================
    // 1. 定义时间
    // ==========================================
    int y, mo, d, h, mi;
    double sec;
    if (!parseISO8601(cfg.targetUTC, y, mo, d, h, mi, sec)) {
        cerr << "Error: malformed targetUTC '" << cfg.targetUTC
             << "' (expected YYYY-MM-DDTHH:MM:SS)\n";
        return 2;
    }

    CivilTime civilTimePredicted(y, mo, d, h, mi, sec);
    CommonTime predictedTime = CivilTime2CommonTime(civilTimePredicted);
    YDSTime ydsPredicted = CommonTime2YDSTime(predictedTime);

    cout << "Target epoch: " << ydsPredicted << endl;

    // ==========================================
    // 2. 加载广播星历
    // ==========================================
    RinexNavStore navStore;
    try {
        navStore.loadFile(navFile);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load navigation file: " << navFile << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }

    // ==========================================
    // 3. 计算卫星位置
    // ==========================================
    SatID sat(cfg.targetSat);
    Xvt xvtNav = navStore.getXvt(sat, predictedTime);

    // ==========================================
    // 4. 精密星历
    // ==========================================
    SP3Store sp3Store;
    try {
        sp3Store.loadSP3File(sp3File);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load precise orbit: " << sp3File << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }
    Xvt xvtSP3 = sp3Store.getXvt(sat, predictedTime);

    // ==========================================
    // 5. 计算差值
    // ==========================================
    Vector3d diffXYZ = xvtNav.getPos() - xvtSP3.getPos();
    Vector3d diffVel = xvtNav.getVel() - xvtSP3.getVel();
    double diffClock = xvtNav.getClockBias() - xvtSP3.getClockBias();

    cout << "========================================" << endl;
    cout << "Epoch    : " << ydsPredicted << endl;
    cout << "Satellite: " << sat << endl;
    cout << endl;

    // 位置
    cout << "--- Position (m) ---" << endl;
    cout << "Broadcast:" << endl;
    cout << xvtNav.x[0] << endl;
    cout << xvtNav.x[1] << endl;
    cout << xvtNav.x[2] << endl;
    cout << "Precise  :" << endl;
    cout << xvtSP3.x[0] << endl;
    cout << xvtSP3.x[1] << endl;
    cout << xvtSP3.x[2] << endl;
    cout << "Diff XYZ :" << endl;
    cout << diffXYZ[0] << endl;
    cout << diffXYZ[1] << endl;
    cout << diffXYZ[2] << endl;
    cout << endl;

    // 速度
    cout << "--- Velocity (m/s) ---" << endl;
    cout << "Broadcast:" << endl;
    cout << xvtNav.v[0] << endl;
    cout << xvtNav.v[1] << endl;
    cout << xvtNav.v[2] << endl;
    cout << "Precise  :" << endl;
    cout << xvtSP3.v[0] << endl;
    cout << xvtSP3.v[1] << endl;
    cout << xvtSP3.v[2] << endl;
    cout << "Diff Vel :" << endl;
    cout << diffVel[0] << endl;
    cout << diffVel[1] << endl;
    cout << diffVel[2] << endl;
    cout << endl;

    // 钟差
    cout << "--- Clock Bias (s) ---" << endl;
    cout << "Broadcast: " << xvtNav.clkbias << endl;
    cout << "Precise  : " << xvtSP3.clkbias << endl;
    cout << "Diff Clk : " << diffClock << endl;

    cout << "========================================" << endl;

    return 0;
}
