/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Single-point positioning (dual-frequency ionosphere-free combination) with
 * simultaneous Doppler-based receiver velocity.
 *
 * Reads a RINEX observation file and a broadcast navigation file, solves each
 * epoch for position and receiver clock, and additionally estimates the receiver
 * velocity from the Doppler observations. Results are written as two text files
 * in the output directory; see docs/data-format.md for the exact column layout,
 * which the Python plotting layer (python/) parses.
 *
 * Usage:
 *   spp_if [config.ini] [options]
 *
 *   config.ini            Configuration file (default: config/spp.ini).
 *                         Relative paths inside it resolve against the config
 *                         file's own directory, not the working directory.
 *                         When the default is not in the working directory the
 *                         parent directories are searched for it, so the program
 *                         also runs from the build tree. A config named
 *                         explicitly on the command line is not searched for.
 *
 * Options (override the config file):
 *   --obs <file>          RINEX observation file
 *   --nav <file>          RINEX broadcast navigation file
 *   --out-dir <dir>       Output directory
 *   --stop <ISO8601>      Stop epoch, e.g. 2025-01-01T00:30:30
 *   --mode <mode>         DUAL_IF | DUAL_RAW
 *   --no-trop             Disable the tropospheric correction
 *   --no-bdstgd           Disable the BeiDou TGD correction
 *   --gps-only            Use GPS observations only
 *   --bds-only            Use BeiDou observations only
 *   --verbose             Per-epoch progress on stdout
 *   -h, --help            This message
 */

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdio>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <system_error>
#include <filesystem>

#include "GnssStruct.h"
#include "CoordConvert.h"
#include "CoordStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "RinexNavStore.hpp"
#include "RinexObsReader.h"
#include "SPPIFCode.h"
#include "SPPVelocity.h"
#include "ConfigData.h"
#include "ConfigReader.h"

using namespace std;

namespace {

void printUsage(const char *prog) {
    cout <<
         "Usage: " << prog << " [config.ini] [options]\n"
         "\n"
         "Single-point positioning (dual-frequency ionosphere-free) with Doppler\n"
         "velocity estimation.\n"
         "\n"
         "  config.ini         Configuration file (default: config/spp.ini)\n"
         "\n"
         "Options:\n"
         "  --obs <file>       RINEX observation file\n"
         "  --nav <file>       RINEX broadcast navigation file\n"
         "  --out-dir <dir>    Output directory\n"
         "  --stop <ISO8601>   Stop epoch, e.g. 2025-01-01T00:30:30\n"
         "  --mode <mode>      DUAL_IF | DUAL_RAW\n"
         "  --no-trop          Disable tropospheric correction\n"
         "  --no-bdstgd        Disable BeiDou TGD correction\n"
         "  --gps-only         GPS observations only\n"
         "  --bds-only         BeiDou observations only\n"
         "  --verbose          Per-epoch progress\n"
         "  -h, --help         This message\n";
}

/// Directory part of a path, or "." when there is none.
string dirOf(const string &path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

/// Filename part of a path, WITHOUT stripping the extension.
///
/// The extension is deliberately kept: output files are named
/// `<obsFileName>_DUAL_IF.spp.out`, so for
/// `WUH200CHN_R_20250010000_01D_30S_MO.rnx` the result is
/// `WUH200CHN_R_20250010000_01D_30S_MO.rnx_DUAL_IF.spp.out`. The Python plotting
/// layer builds the same name and lives with the doubled extension.
string fileNameOf(const string &path) {
    size_t slash = path.find_last_of("/\\");
    return (slash == string::npos) ? path : path.substr(slash + 1);
}

/// Parse "YYYY-MM-DDTHH:MM:SS" (a space instead of 'T' also works).
bool parseISO8601(const string &s, int &y, int &mo, int &d, int &h, int &mi, double &sec) {
    if (s.size() < 19) return false;
    string t = s;
    t[10] = ' ';
    // The remaining separators must be ':' or '-'.
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
bool ensureDirectory(const string &dir) {
    if (dir.empty() || dir == ".") return true;

    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) return true;

    std::filesystem::create_directories(dir, ec);
    // create_directories reports failure if the directory appeared concurrently,
    // so confirm the end state rather than trusting the error code alone.
    return std::filesystem::is_directory(dir, ec);
}

/// True if the path exists and is a regular file.
bool fileExists(const string &path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/spp.ini";
    bool haveConfigArg = false;

    string optObs, optNav, optOutDir, optStop, optMode;
    bool optNoTrop = false, optNoBdsTgd = false;
    bool optGpsOnly = false, optBdsOnly = false, optVerbose = false;

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
        else if (a == "--obs")      optObs = needValue("--obs");
        else if (a == "--nav")      optNav = needValue("--nav");
        else if (a == "--out-dir")  optOutDir = needValue("--out-dir");
        else if (a == "--stop")     optStop = needValue("--stop");
        else if (a == "--mode")     optMode = needValue("--mode");
        else if (a == "--no-trop")  optNoTrop = true;
        else if (a == "--no-bdstgd") optNoBdsTgd = true;
        else if (a == "--gps-only") optGpsOnly = true;
        else if (a == "--bds-only") optBdsOnly = true;
        else if (a == "--verbose")  optVerbose = true;
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
    SPPConfigData cfg = SPPConfigData::defaults();
    string configDir;

    // The default name is written relative to the repository root, but the
    // working directory often is not the repository root - CLion runs a target
    // from its build tree (cmake-build-debug/bin here, two levels down). Walk
    // upwards for the default name so the program still finds its config.
    //
    // Only the default is searched. A file the user named on the command line is
    // taken at its word, so a typo fails loudly instead of quietly running with
    // a different config found somewhere up the tree.
    if (!haveConfigArg && !fileExists(configFile)) {
        string found = findConfigUpwards(configFile);
        if (!found.empty()) {
            configFile = found;
            cerr << "Note: using config found by searching upwards: " << configFile << "\n";
        }
    }

    if (!fileExists(configFile)) {
        if (haveConfigArg) {
            // An explicitly named config file that does not exist is an error.
            cerr << "Error: cannot open config file: " << configFile << "\n";
            return 1;
        }
        // No default config present: fall back to built-in defaults so the
        // program stays usable when run from an arbitrary directory. Note that
        // the built-in paths are relative to the WORKING DIRECTORY - unlike the
        // paths in a config file, which are relative to the config's directory.
        cerr << "Note: " << configFile << " not found; using built-in defaults.\n"
             << "      Relative paths then resolve against the working directory ("
             << std::filesystem::current_path().string() << ").\n";
    } else {
        try {
            cfg = SPPConfigData::fromIni(configFile);
            configDir = dirOf(configFile);
        } catch (const std::exception &e) {
            cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }

    // Command-line overrides win over the config file.
    if (!optObs.empty())    cfg.obsFile = optObs;
    if (!optNav.empty())    cfg.navFile = optNav;
    if (!optOutDir.empty()) cfg.outDir = optOutDir;
    if (!optStop.empty())   cfg.stopUTC = optStop;
    if (optNoTrop)          cfg.tropModel = 0;
    if (optNoBdsTgd)        cfg.enableBDSTGD = false;
    if (optGpsOnly) { cfg.GPS = true; cfg.BD2 = false; cfg.BD3 = false; }
    if (optBdsOnly) { cfg.GPS = false; cfg.BD2 = true; cfg.BD3 = true; }

    // Path resolution policy: every relative path in the config file is resolved
    // against the PROJECT ROOT - the parent of the directory holding the config.
    //
    // For the shipped layout that is the repository root, so `data/foo.rnx` and
    // `output` mean what they appear to mean in config/spp.ini, and the config
    // works from any working directory. One rule for both input and output is
    // easier to reason about than splitting the two, and it is what makes the
    // config file portable.
    //
    // An explicit command-line path is used verbatim (relative to the working
    // directory), which is what someone typing a path at the shell means.
    string projectRoot = configDir.empty() ? "." : dirOf(configDir);

    string roverFile = optObs.empty() ? resolvePath(projectRoot, cfg.obsFile) : cfg.obsFile;
    string navFile = optNav.empty() ? resolvePath(projectRoot, cfg.navFile) : cfg.navFile;
    string outDir = optOutDir.empty() ? resolvePath(projectRoot, cfg.outDir) : cfg.outDir;

    if (optVerbose) {
        cout << "config    : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "obs       : " << roverFile << "\n";
        cout << "nav       : " << navFile << "\n";
        cout << "outDir    : " << outDir << "\n";
        cout << "stop      : " << (cfg.stopUTC.empty() ? "(end of file)" : cfg.stopUTC) << "\n";
        cout << "cutOff    : " << cfg.cutOffElevation << " deg\n";
    }

    if (!ensureDirectory(outDir)) {
        cerr << "Error: cannot create output directory: " << outDir << "\n";
        return 1;
    }

    //---------------------------------------------------------------
    // Stop epoch
    //---------------------------------------------------------------
    bool haveStop = false;
    CommonTime stopEpoch;
    if (!cfg.stopUTC.empty()) {
        int y, mo, d, h, mi;
        double sec;
        if (!parseISO8601(cfg.stopUTC, y, mo, d, h, mi, sec)) {
            cerr << "Error: malformed stopUTC '" << cfg.stopUTC
                 << "' (expected YYYY-MM-DDTHH:MM:SS)\n";
            return 2;
        }
        stopEpoch = CivilTime2CommonTime(CivilTime(y, mo, d, h, mi, sec));
        haveStop = true;
    }

    //---------------------------------------------------------------
    // Open input
    //---------------------------------------------------------------
    std::fstream roverObsStream(roverFile);
    if (!roverObsStream) {
        cerr << "Error: cannot open observation file: " << roverFile << "\n";
        return 1;
    }

    RinexNavStore navStore;
    try {
        navStore.loadFile(navFile);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load navigation file: " << navFile << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }

    //---------------------------------------------------------------
    // Observation types to read
    //---------------------------------------------------------------
    std::map<string, std::set<string>> selectedTypes;
    selectedTypes["G"].insert("C1C");
    selectedTypes["G"].insert("C1W");
    selectedTypes["G"].insert("C1X");
    selectedTypes["G"].insert("C2W");
    selectedTypes["G"].insert("C2X");
    selectedTypes["G"].insert("L1C");
    selectedTypes["G"].insert("L1W");
    selectedTypes["G"].insert("L1X");
    selectedTypes["G"].insert("L2W");
    selectedTypes["G"].insert("L2X");
    selectedTypes["G"].insert("D1C");
    selectedTypes["G"].insert("D1W");
    selectedTypes["G"].insert("D1X");
    selectedTypes["G"].insert("D2W");
    selectedTypes["G"].insert("D2X");

    selectedTypes["C"].insert("C1X");
    selectedTypes["C"].insert("C2I");
    selectedTypes["C"].insert("C6I");
    selectedTypes["C"].insert("C7I");
    selectedTypes["C"].insert("L1X");
    selectedTypes["C"].insert("L2I");
    selectedTypes["C"].insert("L6I");
    selectedTypes["C"].insert("L7I");
    // 北斗多普勒 D2I D6I D7I
    selectedTypes["C"].insert("D2I");
    selectedTypes["C"].insert("D6I");
    selectedTypes["C"].insert("D7I");

    // Ionosphere-free code pairs. GPS: C1C/C2W. BeiDou: B1I(C2) + B3I(C6),
    // matching the Doppler codes D2I / D6I used by the velocity solution.
    std::map<string, std::pair<string, string>> ifCodeTypes;
    ifCodeTypes["G"] = std::make_pair("C1", "C2");
    ifCodeTypes["C"] = std::make_pair("C2", "C6");

    //---------------------------------------------------------------
    // Solver setup
    //---------------------------------------------------------------
    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    SPPIFCode sppif;
    sppif.setRinexNavStore(&navStore);
    sppif.setIFCodeTypes(ifCodeTypes);

    if (optMode == "DUAL_RAW") {
        sppif.setSolveMode(SPPIFCode::DUAL_RAW);
    } else {
        sppif.setSolveMode(SPPIFCode::DUAL_IF_COMB);
    }

    sppif.setTropEnable(cfg.tropModel != 0);
    sppif.setBDSTGDEnable(cfg.enableBDSTGD);
    sppif.setCutOffElev(cfg.cutOffElevation);

    SolverLSQ solver;

    //---------------------------------------------------------------
    // Output files
    //---------------------------------------------------------------
    string obsBase = fileNameOf(roverFile);
    string solFile = outDir + "/" + obsBase + "_DUAL_IF.spp.out";
    string velFileName = outDir + "/" + obsBase + "_pos_vel.out";

    std::fstream solStream(solFile, ios::out);
    if (!solStream) {
        cerr << "Error: cannot open solution file: " << solFile << "\n";
        return 1;
    }

    std::ofstream velOutFile(velFileName, ios::trunc);
    if (!velOutFile) {
        cerr << "Error: cannot open velocity file: " << velFileName << "\n";
        return 1;
    }
    velOutFile << fixed << setprecision(6);
    velOutFile << "sod X Y Z vE vN vU recClkDot\n";

    //---------------------------------------------------------------
    // Epoch loop
    //---------------------------------------------------------------
    long epochCount = 0;
    long velocityCount = 0;

    while (true) {
        ObsData roverData;
        try {
            roverData = readObsRover.parseRinexObs();
        }
        catch (EndOfFile &e) {
            break;
        }

        // Constellation filtering.
        SatIDSet delSat;
        for (auto &stv: roverData.satTypeValueData) {
            string sys = stv.first.system;
            if (optBdsOnly && sys != "C") delSat.insert(stv.first);
            if (optGpsOnly && sys != "G") delSat.insert(stv.first);
            if (!cfg.GPS && sys == "G") delSat.insert(stv.first);
            if (!cfg.BD2 && !cfg.BD3 && sys == "C") delSat.insert(stv.first);
        }
        for (auto sat: delSat)
            roverData.satTypeValueData.erase(sat);

        CommonTime epoch = roverData.epoch;
        YDSTime ydst = CommonTime2YDSTime(epoch);
        double sod = ydst.sod;

        XYZ posXYZ;
        Eigen::Vector3d velXYZ(0, 0, 0);
        double recClkDot = 0.0;
        bool velOk = sppif.runPosVel(roverData, posXYZ, velXYZ, recClkDot);

        Vector3d xyzRover = sppif.getXYZ();
        printSolution(solStream, epoch, xyzRover);
        epochCount++;

        Eigen::Vector3d velENU(0, 0, 0);
        if (velOk) {
            velENU = sppif.getVelObj().xyz2Enu(posXYZ, velXYZ);
            velocityCount++;
            if (optVerbose)
                cout << "[Vel] E/N/U: " << velENU.transpose()
                     << " clkDot: " << recClkDot << "\n";
        }

        velOutFile << std::fixed << std::setprecision(6)
                   << sod << " "
                   << posXYZ[0] << " " << posXYZ[1] << " " << posXYZ[2] << " "
                   << velENU[0] << " " << velENU[1] << " " << velENU[2] << " "
                   << recClkDot << "\n";

        if (haveStop && roverData.epoch > stopEpoch)
            break;
    }

    roverObsStream.close();
    solStream.close();
    velOutFile.close();

    //---------------------------------------------------------------
    // Manifest
    //---------------------------------------------------------------
    // Records which files this run produced and the settings that produced them,
    // so the Python layer does not have to guess a filename by convention and
    // cannot accidentally analyse a stale output.
    string manifestFile = outDir + "/" + obsBase + "_manifest.json";
    std::ofstream mf(manifestFile, ios::trunc);
    if (mf) {
        mf << "{\n";
        mf << "  \"obs\": \"" << roverFile << "\",\n";
        mf << "  \"nav\": \"" << navFile << "\",\n";
        mf << "  \"sppOut\": \"" << solFile << "\",\n";
        mf << "  \"posVelOut\": \"" << velFileName << "\",\n";
        mf << "  \"epochs\": " << epochCount << ",\n";
        mf << "  \"epochsWithVelocity\": " << velocityCount << ",\n";
        mf << "  \"stopUTC\": \"" << cfg.stopUTC << "\",\n";
        mf << "  \"cutOffElevation\": " << cfg.cutOffElevation << ",\n";
        mf << "  \"runOnlyGPS\": " << (optGpsOnly ? "true" : "false") << ",\n";
        mf << "  \"runOnlyBDS\": " << (optBdsOnly ? "true" : "false") << ",\n";
        mf << "  \"tropEnabled\": " << ((cfg.tropModel != 0) ? "true" : "false") << ",\n";
        mf << "  \"bdsTgdEnabled\": " << (cfg.enableBDSTGD ? "true" : "false") << "\n";
        mf << "}\n";
        mf.close();
    }

    cout << "Epochs processed : " << epochCount << "\n";
    cout << "Epochs with velocity: " << velocityCount << "\n";
    cout << "Position  -> " << solFile << "\n";
    cout << "Pos + vel -> " << velFileName << "\n";

    return 0;
}
