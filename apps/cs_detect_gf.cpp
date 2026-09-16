/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Carrier-phase geometry-free (GF) cycle-slip detection.
 *
 * The observable is L_I = L1 - L2, which removes the geometry, the orbit and
 * satellite-clock errors and the troposphere, leaving the ionosphere, hardware
 * delays, phase wind-up and the ambiguities. Two detectors are provided, both
 * comparing an observed epoch against a prediction built only from earlier
 * epochs:
 *
 *   diff  - single epoch difference, ΔL_I = L_I(i) - L_I(i-1), tested against
 *           a recursive mean and variance of the difference series. Chapter
 *           7.3, exercise 1.
 *   poly  - second-order polynomial fitted to a sliding window of past epochs
 *           and extrapolated to the current one. The ionosphere varies far too
 *           much between epochs at 30 s sampling for the difference to work,
 *           and the quadratic absorbs it. Exercises 2 and 3.
 *
 * Rows are written for every epoch that could be evaluated, including the ones
 * where no judgement was made, and the status column says which case it was:
 *
 *   0 OK       tested, no slip
 *   1 SLIP     tested, slip reported at this epoch
 *   2 INIT     first epoch of an arc - nothing to difference against
 *   3 GAP      gap longer than deltaTMax - the arc restarted
 *   4 WARMUP   not enough samples in the polynomial window yet
 *
 * Only 1 means "slip". The MW example program collapses 1, 2 and 3 into a
 * single flag, which makes every satellite's first epoch a guaranteed false
 * positive and leaves the output impossible to score.
 *
 * Usage:
 *   cs_detect_gf [config.ini] [options]
 *
 *   config.ini            Configuration file (default: config/cs.ini).
 *                         Relative paths inside it resolve against the config
 *                         file's own directory, not the working directory.
 *
 * Options (override the config file):
 *   --obs <file>          RINEX observation file
 *   --out-dir <dir>       Output directory
 *   --stop <ISO8601>      Stop epoch
 *   --mode <mode>         diff | poly | both (default: both)
 *   --threshold <m>       Detection threshold floor, default 0.030
 *   --window <n>          Polynomial window length in epochs, default 30
 *   --delta-t-max <s>     Arc-interruption threshold, default 120
 *   --verbose             Per-epoch progress on stdout
 *   -h, --help            This message
 *
 * Output, in the output directory:
 *   <sat>.gf.diff    sat ydtime LI dLI meanDL sigmaDL flag status
 *   <sat>.gf.poly    sat ydtime LI Shat resid sigmaRes flag status
 *   summary.<mode>.csv
 */

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <vector>
#include "GnssStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "ConfigData.h"
#include "Exception.h"
#include "app_utils.h"

using namespace std;

namespace {

void printUsage(const char *prog) {
    cout << "Usage: " << prog << " [config.ini] [options]\n"
         << "\n"
         << "  config.ini         Configuration file (default: config/cs.ini)\n"
         << "  --obs <file>       RINEX observation file\n"
         << "  --out-dir <dir>    Output directory\n"
         << "  --stop <ISO8601>   Stop epoch\n"
         << "  --mode <mode>      diff | poly | both (default: both)\n"
         << "  --threshold <m>    Detection threshold floor (default: 0.030)\n"
         << "  --window <n>       Polynomial window length, epochs (default: 30)\n"
         << "  --delta-t-max <s>  Arc-interruption threshold (default: 120)\n"
         << "  --verbose          Per-epoch progress on stdout\n"
         << "  -h, --help         This message\n";
}

/// Value of `sat` at `epoch`, or NaN when the detector produced none.
double lookup(const SatEpochValueMap &data, const SatID &sat, const CommonTime &epoch) {
    auto satIt = data.find(sat);
    if (satIt == data.end()) return std::numeric_limits<double>::quiet_NaN();
    auto epochIt = satIt->second.find(epoch);
    if (epochIt == satIt->second.end()) return std::numeric_limits<double>::quiet_NaN();
    return epochIt->second;
};

/// Counts for one satellite, used to build the summary.
struct StatusCounts {
    int nEpochs = 0;
    int nOk = 0;
    int nSlip = 0;
    int nInit = 0;
    int nGap = 0;
    int nWarmup = 0;
    int nTested() const { return nOk + nSlip; }
};

void writeSummary(const string &path,
                  const std::map<SatID, StatusCounts> &counts,
                  const CSConfigData &cfg,
                  const string &obsFile,
                  const string &modeName) {
    std::ofstream out(path);
    if (!out) {
        cerr << "Error: cannot write " << path << "\n";
        return;
    }

    out << "sat,nEpochs,nTested,nSlip,nOk,nInit,nGap,nWarmup,slipRate\n";

    int totalEpochs = 0, totalTested = 0, totalSlip = 0;
    for (const auto &entry: counts) {
        const SatID &sat = entry.first;
        const StatusCounts &c = entry.second;

        totalEpochs += c.nEpochs;
        totalTested += c.nTested();
        totalSlip += c.nSlip;

        out << sat.toString() << "," << c.nEpochs << "," << c.nTested() << ","
            << c.nSlip << "," << c.nOk << "," << c.nInit << ","
            << c.nGap << "," << c.nWarmup << ","
            << std::fixed << std::setprecision(6)
            << (c.nTested() > 0 ? static_cast<double>(c.nSlip) / c.nTested() : 0.0)
            << "\n";
    }
    out << "TOTAL," << totalEpochs << "," << totalTested << "," << totalSlip
        << ",,,,,";

    out << "\n# mode," << modeName << "\n";
    out << "# obsFile," << obsFile << "\n";
    out << "# threshold_m," << cfg.threshold << "\n";
    out << "# deltaTMax_s," << cfg.deltaTMax << "\n";
    out << "# gfPolyWindow," << cfg.gfPolyWindow << "\n";
    out << "# gps," << (cfg.GPS ? 1 : 0) << "\n";
    out << "# bds," << (cfg.BD2 ? 1 : 0) << "\n";
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/cs.ini";
    bool haveConfigArg = false;

    string optObs, optOutDir, optStop, optMode;
    string optThreshold, optWindow, optDeltaTMax;
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
        else if (a == "--obs")          optObs = needValue("--obs");
        else if (a == "--out-dir")      optOutDir = needValue("--out-dir");
        else if (a == "--stop")         optStop = needValue("--stop");
        else if (a == "--mode")         optMode = needValue("--mode");
        else if (a == "--threshold")    optThreshold = needValue("--threshold");
        else if (a == "--window")       optWindow = needValue("--window");
        else if (a == "--delta-t-max")  optDeltaTMax = needValue("--delta-t-max");
        else if (a == "--verbose")      optVerbose = true;
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
    CSConfigData cfg = CSConfigData::defaults();
    string configDir;

    // The default config name is relative to the repository root, but the working
    // directory is often the build tree (CLion runs a target from its binary
    // directory). Search upwards for the default; an explicitly named file is
    // taken at its word. See findConfigUpwards() in ConfigData.h.
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
        cerr << "Note: " << configFile << " not found; using built-in defaults.\n";
    } else {
        try {
            cfg = CSConfigData::fromIni(configFile);
            configDir = dirOf(configFile);
        } catch (const std::exception &e) {
            cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }

    if (!optObs.empty())    cfg.obsFile = optObs;
    if (!optOutDir.empty()) cfg.outDir = optOutDir;
    if (!optStop.empty())   cfg.stopUTC = optStop;

    try {
        if (!optThreshold.empty())  cfg.threshold = std::stod(optThreshold);
        if (!optDeltaTMax.empty())  cfg.deltaTMax = std::stod(optDeltaTMax);
        if (!optWindow.empty())     cfg.gfPolyWindow = std::stoi(optWindow);
    } catch (const std::exception &) {
        cerr << "Error: --threshold, --window and --delta-t-max must be numeric\n";
        return 2;
    }

    bool runDiff = true, runPoly = true;
    if (!optMode.empty()) {
        if (optMode == "diff")      { runPoly = false; }
        else if (optMode == "poly") { runDiff = false; }
        else if (optMode != "both") {
            cerr << "Error: --mode must be diff, poly or both\n";
            return 2;
        }
    }

    string modeName = (runDiff && runPoly) ? "both" : (runDiff ? "diff" : "poly");

    string projectRoot = configDir.empty() ? "." : dirOf(configDir);
    string obsFile = optObs.empty() ? resolvePath(projectRoot, cfg.obsFile) : cfg.obsFile;
    string outDir = optOutDir.empty() ? resolvePath(projectRoot, cfg.outDir) : cfg.outDir;

    if (optVerbose) {
        cout << "config      : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "obs         : " << obsFile << "\n";
        cout << "outDir      : " << outDir << "\n";
        cout << "mode        : " << modeName << "\n";
        cout << "threshold   : " << cfg.threshold << " m\n";
        cout << "deltaTMax   : " << cfg.deltaTMax << " s\n";
        cout << "polyWindow  : " << cfg.gfPolyWindow << "\n";
        cout << "stop        : " << (cfg.stopUTC.empty() ? "(end of file)" : cfg.stopUTC) << "\n";
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
    std::fstream obsStream(obsFile);
    if (!obsStream) {
        cerr << "Error: cannot open observation file: " << obsFile << "\n";
        return 1;
    }

    std::map<string, std::set<string>> sysTypes = cycleSlipObsTypes(cfg.GPS, cfg.BD2);

    //---------------------------------------------------------------
    // Epoch loop
    //---------------------------------------------------------------
    SatEpochValueMap gfData;

    SatEpochValueMap diffDLData, diffMeanData, diffSigmaData, diffFlagData;
    SatEpochValueMap polyPredData, polyResData, polySigmaData, polyFlagData;

    int nEpoch = 0;
    while (true) {
        ObsData obsData;

        try {
            obsData = parseRinexObs(obsStream);
        } catch (EndOfFile &) {
            break;
        }

        if (haveStop && obsData.epoch > stopEpoch)
            break;

        chooseObs(obsData, sysTypes);
        convertObsType(obsData);

        // diff first: both detectors drop satellites for which the combination
        // cannot be formed, and they use identical criteria, so the second one
        // sees exactly the set the first left behind.
        if (runDiff) {
            std::map<Variable, int> csFlagData;
            detectCSGFdiff(obsData, csFlagData, gfData,
                           diffDLData, diffMeanData, diffSigmaData, diffFlagData,
                           cfg.threshold, cfg.deltaTMax);
        }

        if (runPoly) {
            std::map<Variable, int> csFlagData;
            detectCSGFpoly(obsData, csFlagData, gfData,
                           polyPredData, polyResData, polySigmaData, polyFlagData,
                           cfg.gfPolyWindow, cfg.threshold, cfg.deltaTMax);
        }

        ++nEpoch;

        if (optVerbose && (nEpoch % 500 == 0))
            cout << "epoch " << nEpoch << "\n";
    }

    obsStream.close();

    if (nEpoch == 0) {
        cerr << "Error: no epochs read from " << obsFile
             << " (is the observation type table right for this file?)\n";
        return 1;
    }

    //---------------------------------------------------------------
    // Per-satellite output
    //---------------------------------------------------------------
    std::map<SatID, StatusCounts> diffCounts, polyCounts;

    for (const auto &sd: gfData) {
        const SatID &sat = sd.first;

        if (runDiff) {
            string path = outDir + "/" + sat.toString() + ".gf.diff";
            std::ofstream out(path);
            if (!out) {
                cerr << "Error: cannot write " << path << "\n";
                return 1;
            }

            out << "# sat year doy sod timeSystem LI_m dLI_m meanDL_m sigmaDL_m flag status\n";
            out << "# dLI is the epoch difference of LI; flag 1 = slip, 2 = arc start,\n";
            out << "# 3 = data gap. Untested epochs carry nan in the statistic columns.\n";

            for (const auto &ed: sd.second) {
                const CommonTime &epoch = ed.first;
                double flag = lookup(diffFlagData, sat, epoch);
                int status = static_cast<int>(flag + 0.5);

                StatusCounts &c = diffCounts[sat];
                c.nEpochs++;
                switch (status) {
                    case CSGF_SLIP:   c.nSlip++;   break;
                    case CSGF_OK:     c.nOk++;     break;
                    case CSGF_INIT:   c.nInit++;   break;
                    case CSGF_GAP:    c.nGap++;    break;
                    default:          c.nWarmup++; break;
                }

                out << sat << " " << CommonTime2YDSTime(epoch) << " "
                    << std::fixed << std::setprecision(6)
                    << ed.second << " "
                    << lookup(diffDLData, sat, epoch) << " "
                    << lookup(diffMeanData, sat, epoch) << " "
                    << lookup(diffSigmaData, sat, epoch) << " "
                    << status << " " << csGFStatusName(status) << "\n";
            }
        }

        if (runPoly) {
            string path = outDir + "/" + sat.toString() + ".gf.poly";
            std::ofstream out(path);
            if (!out) {
                cerr << "Error: cannot write " << path << "\n";
                return 1;
            }

            out << "# sat year doy sod timeSystem LI_m Shat_m resid_m sigmaRes_m flag status\n";
            out << "# Shat is the polynomial prediction from previous epochs only\n";
            out << "# (out-of-sample); resid = LI - Shat. flag 1 = slip, 2 = arc start,\n";
            out << "# 3 = data gap, 4 = window still warming up.\n";

            for (const auto &ed: sd.second) {
                const CommonTime &epoch = ed.first;
                double flag = lookup(polyFlagData, sat, epoch);
                int status = static_cast<int>(flag + 0.5);

                StatusCounts &c = polyCounts[sat];
                c.nEpochs++;
                switch (status) {
                    case CSGF_SLIP:   c.nSlip++;   break;
                    case CSGF_OK:     c.nOk++;     break;
                    case CSGF_INIT:   c.nInit++;   break;
                    case CSGF_GAP:    c.nGap++;    break;
                    default:          c.nWarmup++; break;
                }

                double pred = lookup(polyPredData, sat, epoch);
                double resid = lookup(polyResData, sat, epoch);

                out << sat << " " << CommonTime2YDSTime(epoch) << " "
                    << std::fixed << std::setprecision(6)
                    << ed.second << " "
                    << pred << " "
                    << resid << " "
                    << lookup(polySigmaData, sat, epoch) << " "
                    << status << " " << csGFStatusName(status) << "\n";
            }
        }
    }

    //---------------------------------------------------------------
    // Summary
    //---------------------------------------------------------------
    if (runDiff)
        writeSummary(outDir + "/summary.gf.diff.csv", diffCounts, cfg, obsFile, "diff");
    if (runPoly)
        writeSummary(outDir + "/summary.gf.poly.csv", polyCounts, cfg, obsFile, "poly");

    cout << "Read " << nEpoch << " epochs from " << obsFile << "\n";
    cout << "Wrote " << gfData.size() << " satellite file(s) (mode " << modeName
         << ") to " << outDir << "\n";

    return 0;
}
