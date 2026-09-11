/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Melbourne-Wuebbena (MW) cycle-slip detection.
 *
 * For every epoch this forms the wide-lane ambiguity from the two carrier
 * phases and the two pseudoranges, then compares it against a recursive mean
 * and variance of its own history. A gap in tracking, or a jump exceeding the
 * threshold, is reported as a cycle slip. See chapter 7.1-7.2 and
 * docs/cycle-slip-gf.md.
 *
 * Every epoch that can be evaluated is written out, one file per satellite,
 * including the epochs where nothing was decided. Those are data: the first
 * epoch of an arc is structurally not a slip, and hiding it would make the
 * output impossible to score.
 *
 * Usage:
 *   cs_detect_mw [config.ini] [options]
 *
 *   config.ini            Configuration file (default: config/cs.ini).
 *                         Relative paths inside it resolve against the config
 *                         file's own directory, not the working directory.
 *
 * Options (override the config file):
 *   --obs <file>          RINEX observation file
 *   --out-dir <dir>       Output directory
 *   --stop <ISO8601>      Stop epoch, e.g. 2022-03-03T07:00:00
 *   --verbose             Per-epoch progress on stdout
 *   -h, --help            This message
 *
 * Output, in the output directory:
 *   <sat>            per satellite: sat ydtime mw meanMW csFlagArg flag
 *   summary.mw.csv   per satellite counts and the parameters used
 */

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
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
         << "  --stop <ISO8601>   Stop epoch, e.g. 2022-03-03T07:00:00\n"
         << "  --verbose          Per-epoch progress on stdout\n"
         << "  -h, --help         This message\n";
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/cs.ini";
    bool haveConfigArg = false;

    string optObs, optOutDir, optStop;
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
        else if (a == "--obs")      optObs = needValue("--obs");
        else if (a == "--out-dir")  optOutDir = needValue("--out-dir");
        else if (a == "--stop")     optStop = needValue("--stop");
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
    CSConfigData cfg = CSConfigData::defaults();
    string configDir;

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

    // Command-line overrides win over the config file.
    if (!optObs.empty())    cfg.obsFile = optObs;
    if (!optOutDir.empty()) cfg.outDir = optOutDir;
    if (!optStop.empty())   cfg.stopUTC = optStop;

    // Relative config paths resolve against the parent of the config's
    // directory - the repository root for the shipped layout. An explicit
    // command-line path is used verbatim.
    string projectRoot = configDir.empty() ? "." : dirOf(configDir);

    string obsFile = optObs.empty() ? resolvePath(projectRoot, cfg.obsFile) : cfg.obsFile;
    string outDir = optOutDir.empty() ? resolvePath(projectRoot, cfg.outDir) : cfg.outDir;

    if (optVerbose) {
        cout << "config  : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "obs     : " << obsFile << "\n";
        cout << "outDir  : " << outDir << "\n";
        cout << "stop    : " << (cfg.stopUTC.empty() ? "(end of file)" : cfg.stopUTC) << "\n";
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
    // Everything the detector produces, kept for the whole run so the
    // per-satellite files can be written after the input is exhausted.
    SatEpochValueMap satEpochMWData;
    SatEpochValueMap satEpochMeanMWData;
    SatEpochValueMap satEpochCSFlagData;

    // detectCSMW stores `csFlag * mwValue` in satEpochCSFlagData so the plotted
    // quantity has the same scale as the MW series. That makes the column
    // useless as a flag - it is zero both when there is no slip and whenever
    // the MW value happens to cross zero. Keep the real 0/1 flag separately.
    std::map<SatID, std::map<CommonTime, int>> satEpochFlagData;

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

        std::map<Variable, int> csFlagData;
        detectCSMW(obsData,
                   csFlagData,
                   satEpochMWData,
                   satEpochMeanMWData,
                   satEpochCSFlagData);

        // The slip is flagged on both carrier ambiguities with the same value,
        // so taking either one per satellite is enough.
        for (const auto &entry: csFlagData) {
            satEpochFlagData[entry.first.getSat()][obsData.epoch] = entry.second;
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
    for (const auto &sd: satEpochMWData) {
        const SatID &sat = sd.first;
        string satFile = outDir + "/" + sat.toString() + ".mw";

        std::ofstream out(satFile);
        if (!out) {
            cerr << "Error: cannot write " << satFile << "\n";
            return 1;
        }

        out << "# sat year doy sod timeSystem mw_m meanMW_m csFlagArg flag\n";
        out << "# flag 1 = cycle slip reported at this epoch (includes the first\n";
        out << "# epoch of an arc and the first epoch after a data gap - the MW\n";
        out << "# detector does not separate those cases).\n";

        for (const auto &ed: sd.second) {
            const CommonTime &epoch = ed.first;

            auto flagIt = satEpochFlagData.find(sat);
            int flag = 0;
            if (flagIt != satEpochFlagData.end()) {
                auto epochIt = flagIt->second.find(epoch);
                if (epochIt != flagIt->second.end())
                    flag = epochIt->second;
            }

            out << sat << " "
                << CommonTime2YDSTime(epoch) << " "
                << std::fixed << std::setprecision(3)
                << ed.second << " "
                << satEpochMeanMWData[sat][epoch] << " "
                << satEpochCSFlagData[sat][epoch] << " "
                << flag << "\n";
        }
    }

    //---------------------------------------------------------------
    // Summary
    //---------------------------------------------------------------
    string summaryFile = outDir + "/summary.mw.csv";
    std::ofstream summary(summaryFile);
    if (!summary) {
        cerr << "Error: cannot write " << summaryFile << "\n";
        return 1;
    }

    summary << "sat,nEpochs,nFlagged,nSlipRate\n";
    int totalEpochs = 0, totalFlagged = 0;
    for (const auto &sd: satEpochMWData) {
        int flagged = 0;
        for (const auto &ed: sd.second) {
            auto flagIt = satEpochFlagData.find(sd.first);
            if (flagIt != satEpochFlagData.end() &&
                flagIt->second.count(ed.first) && flagIt->second.at(ed.first) != 0)
                ++flagged;
        }
        int n = static_cast<int>(sd.second.size());
        totalEpochs += n;
        totalFlagged += flagged;

        summary << sd.first.toString() << "," << n << "," << flagged << ","
                << std::fixed << std::setprecision(6)
                << (n > 0 ? static_cast<double>(flagged) / n : 0.0) << "\n";
    }
    summary << "TOTAL," << totalEpochs << "," << totalFlagged << "\n";
    summary << "# deltaTMax_s," << cfg.deltaTMax << "\n";
    summary << "# obsFile," << obsFile << "\n";
    summary << "# gps," << (cfg.GPS ? 1 : 0) << "\n";
    summary << "# bds," << (cfg.BD2 ? 1 : 0) << "\n";
    summary << "# note,MW does not separate arc-start / data-gap / slip;\n";
    summary << "# note,use the residual analysis in docs/cycle-slip-gf.md\n";

    cout << "Read " << nEpoch << " epochs from " << obsFile << "\n";
    cout << "Wrote " << satEpochMWData.size() << " satellite files and summary.mw.csv to "
         << outDir << "\n";

    return 0;
}
