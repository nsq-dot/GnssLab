/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * RINEX observation inventory: per-epoch satellite counts by constellation,
 * written as a CSV for the plotting layer, plus a summary report on stdout.
 *
 * Reads a RINEX observation file and a broadcast navigation file. The navigation
 * file is loaded (and thereby validated) but nothing is computed from it here;
 * it is required by the shared BiasConfigData both this program and system_bias
 * read, and the load doubles as a check that the file is readable.
 *
 * Usage:
 *   read_rinex [config.ini] [options]
 *
 *   config.ini            Configuration file (default: config/bias.ini).
 *                         Relative paths inside it resolve against the config
 *                         file's own directory, not the working directory.
 *                         When the default is not in the working directory the
 *                         parent directories are searched for it. A config named
 *                         explicitly on the command line is not searched for.
 *
 * Options (override the config file):
 *   --obs <file>          RINEX observation file
 *   --nav <file>          RINEX broadcast navigation file
 *   --out-dir <dir>       Output directory
 *   --stop <ISO8601>      Stop after this epoch, e.g. 2025-01-01T00:30:30
 *   --verbose             Per-epoch progress on stdout
 *   -h, --help            This message
 *
 * Output: <outDir>/GNSS_Statistics.csv with the columns
 *   Epoch,GPS,BDS,Galileo,GLONASS,Total
 */

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <map>
#include <vector>

#include "GnssStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "RinexNavStore.hpp"
#include "RinexObsReader.h"
#include "ConfigData.h"
#include "app_utils.h"

using namespace std;

namespace {

void printUsage(const char *prog) {
    cout <<
         "Usage: " << prog << " [config.ini] [options]\n"
         "\n"
         "RINEX observation inventory and per-constellation statistics.\n"
         "\n"
         "  config.ini         Configuration file (default: config/bias.ini)\n"
         "\n"
         "Options:\n"
         "  --obs <file>       RINEX observation file\n"
         "  --nav <file>       RINEX broadcast navigation file\n"
         "  --out-dir <dir>    Output directory\n"
         "  --stop <ISO8601>   Stop after this epoch, e.g. 2025-01-01T00:30:30\n"
         "  --verbose          Per-epoch progress\n"
         "  -h, --help         This message\n";
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/bias.ini";
    bool haveConfigArg = false;

    string optObs, optNav, optOutDir, optStop;
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
        else if (a == "--obs")     optObs = needValue("--obs");
        else if (a == "--nav")     optNav = needValue("--nav");
        else if (a == "--out-dir") optOutDir = needValue("--out-dir");
        else if (a == "--stop")    optStop = needValue("--stop");
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
    BiasConfigData cfg = BiasConfigData::defaults();
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
            cfg = BiasConfigData::fromIni(configFile);
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

    // Relative config paths resolve against the project root; an explicit
    // command-line path is used verbatim. See apps/spp_if.cpp for the rule.
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

    // ====================== GPS + 北斗 + Galileo + GLONASS======================
    std::map<string, std::set<string>> selectedTypes;

    // GPS (G)
    selectedTypes["G"].insert("C1C");
    selectedTypes["G"].insert("C1W");
    selectedTypes["G"].insert("C1X");
    selectedTypes["G"].insert("C2W");
    selectedTypes["G"].insert("C2X");
    selectedTypes["G"].insert("C5X");
    selectedTypes["G"].insert("L1C");
    selectedTypes["G"].insert("L1W");
    selectedTypes["G"].insert("L1X");
    selectedTypes["G"].insert("L2W");
    selectedTypes["G"].insert("L2X");
    selectedTypes["G"].insert("L5X");

    // 北斗 BDS (C)
    selectedTypes["C"].insert("C1X");
    selectedTypes["C"].insert("C2I");
    selectedTypes["C"].insert("C5X");
    selectedTypes["C"].insert("C6I");
    selectedTypes["C"].insert("C7D");
    selectedTypes["C"].insert("C7I");
    selectedTypes["C"].insert("C7Z");
    selectedTypes["C"].insert("C8X");
    selectedTypes["C"].insert("L1X");
    selectedTypes["C"].insert("L2I");
    selectedTypes["C"].insert("L5X");
    selectedTypes["C"].insert("L6I");
    selectedTypes["C"].insert("L7D");
    selectedTypes["C"].insert("L7I");
    selectedTypes["C"].insert("L7Z");
    selectedTypes["C"].insert("L8X");

    // 伽利略 Galileo (E)
    selectedTypes["E"].insert("C1X");
    selectedTypes["E"].insert("C5X");
    selectedTypes["E"].insert("C6X");
    selectedTypes["E"].insert("C7X");
    selectedTypes["E"].insert("C8X");
    selectedTypes["E"].insert("L1X");
    selectedTypes["E"].insert("L5X");
    selectedTypes["E"].insert("L6X");
    selectedTypes["E"].insert("L7X");
    selectedTypes["E"].insert("L8X");

    // GLONASS (R)
    selectedTypes["R"].insert("C1C");
    selectedTypes["R"].insert("C1P");
    selectedTypes["R"].insert("C2C");
    selectedTypes["R"].insert("C2P");
    selectedTypes["R"].insert("C3X");
    selectedTypes["R"].insert("L1C");
    selectedTypes["R"].insert("L1P");
    selectedTypes["R"].insert("L2C");
    selectedTypes["R"].insert("L2P");
    selectedTypes["R"].insert("L3X");
    // ======================================================================

    //-------------------
    // 定义数据处理的对象
    //-------------------
    //>>> classes for rover
    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    // ====================== 全局统计变量 ======================
    int total_epoch = 0;  // 总历元数

    // 各系统卫星集合（自动去重）
    // NOTE: these are declared but never inserted into, so the summary report
    // below always prints 0 for each system while the CSV is correct. Known
    // issue, recorded in docs/roadmap.md - not fixed here because it changes the
    // report the frozen output was produced with.
    set<SatID> sat_G, sat_C, sat_E, sat_R;

    // 观测值计数：key = "G:C1C"，value = 次数
    map<string, int> obs_count;

    // 单历元最大卫星数
    int max_sat_in_epoch = 0;

    // ================= 新增：历元卫星数量变化曲线 =================
    vector<int> epoch_list;        // 历元编号
    vector<int> gps_count_list;    // 每个历元 GPS 卫星数
    vector<int> bds_count_list;    // 每个历元 BDS 卫星数
    vector<int> gal_count_list;    // Galileo
    vector<int> glo_count_list;    // GLONASS
    vector<int> total_sat_list;    // 总卫星数

    while (true)
    {
        ObsData roverData;

        try
        {
            roverData = readObsRover.parseRinexObs();
            if (optVerbose) cout << "roverData:" << roverData << endl;

            // ====================== 统计开始 ======================
            total_epoch++;
            int numG = 0, numC = 0, numE = 0, numR = 0;

            for (auto& sat_entry : roverData.satTypeValueData)
            {
                SatID sat = sat_entry.first;
                string sys = sat.system;

                // 记录卫星
                if (sys == "G") numG++;
                if (sys == "C") numC++;
                if (sys == "E") numE++;
                if (sys == "R") numR++;

            }
            int total = numG + numC + numE + numR;

            // 保存到序列（画图用）
            epoch_list.push_back(total_epoch);
            gps_count_list.push_back(numG);
            bds_count_list.push_back(numC);
            gal_count_list.push_back(numE);
            glo_count_list.push_back(numR);
            total_sat_list.push_back(total);

            if (total > max_sat_in_epoch)
                max_sat_in_epoch = total;
        }
        catch (EndOfFile &e) { break; }

        if (haveStop && roverData.epoch > stopEpoch)
            break;
    }

    roverObsStream.close();

    // ====================== 输出 CSV 文件用于 Python 绘图 ======================
    string csvFile = outDir + "/GNSS_Statistics.csv";
    ofstream csvStream(csvFile);
    if (!csvStream) {
        cerr << "Error: cannot open solution file: " << csvFile << "\n";
        return 1;
    }
    csvStream << "Epoch,GPS,BDS,Galileo,GLONASS,Total\n";
    for (int i = 0; i < epoch_list.size(); i++) {
        csvStream << epoch_list[i] << ","
                  << gps_count_list[i] << ","
                  << bds_count_list[i] << ","
                  << gal_count_list[i] << ","
                  << glo_count_list[i] << ","
                  << total_sat_list[i] << "\n";
    }
    csvStream.close();

    // ====================== 输出完整统计报告 ======================
    cout << endl;
    cout << "================================================" << endl;
    cout << "            GNSS OBSERVATION STATISTICS        " << endl;
    cout << "================================================" << endl;
    cout << "Total epochs              : " << total_epoch << endl;
    cout << "Max satellites per epoch  : " << max_sat_in_epoch << endl;
    cout << "GPS satellites             : " << sat_G.size() << endl;
    cout << "BDS satellites             : " << sat_C.size() << endl;
    cout << "Galileo satellites         : " << sat_E.size() << endl;
    cout << "GLONASS satellites         : " << sat_R.size() << endl;
    cout << "-----------------------------------------------" << endl;
    cout << "Observation types count:" << endl;
    for (auto& pair : obs_count)
    {
        cout << "  " << pair.first << "  : " << pair.second << endl;
    }
    cout << "================================================" << endl;
    // ==============================================================

    cout << "Epochs processed : " << total_epoch << "\n";
    cout << "Statistics       -> " << csvFile << "\n";

    return 0;
}
