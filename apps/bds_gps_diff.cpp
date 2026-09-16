/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Broadcast-versus-precise orbit and clock comparison across a satellite set and
 * a time series: for every epoch and every satellite, the position and velocity
 * difference between the broadcast ephemeris and the MGEX precise orbit, written
 * as a CSV for the plotting layer.
 *
 * The single-satellite, single-epoch counterpart is apps/bds_eph.
 *
 * Usage:
 *   bds_gps_diff [config.ini] [options]
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
 *   --out-dir <dir>       Output directory
 *   --sats <list>         Comma-separated satellites, e.g. "G02,C01"
 *   --start <ISO8601>     First epoch of the series
 *   --epochs <n>          Number of epochs
 *   --interval <seconds>  Seconds between epochs
 *   --verbose             Sparse progress on stdout
 *   -h, --help            This message
 *
 * There is no --stop: the series is parameterised by an epoch count, and adding
 * a stop epoch as well would need a rounding rule for the last partial step and
 * would read as the opposite of --start.
 *
 * Output: <outDir>/sat_pos_vel_diff_<yyyymmdd>.csv, named after the start date.
 */

#include "TimeStruct.h"
#include "TimeConvert.h"
#include "GnssStruct.h"
#include "RinexNavStore.hpp"
#include "SP3Store.hpp"
#include "ConfigData.h"
#include "app_utils.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <string>
#include <sstream>
#include <cctype>

using namespace std;

namespace {

void printUsage(const char *prog) {
    cout <<
         "Usage: " << prog << " [config.ini] [options]\n"
         "\n"
         "Broadcast versus precise orbit/clock, full constellation, time series.\n"
         "\n"
         "  config.ini         Configuration file (default: config/eph.ini)\n"
         "\n"
         "Options:\n"
         "  --nav <file>       RINEX broadcast navigation file\n"
         "  --sp3 <file>       MGEX precise orbit (SP3)\n"
         "  --out-dir <dir>    Output directory\n"
         "  --sats <list>      Comma-separated satellites, e.g. \"G02,C01\"\n"
         "  --start <ISO8601>  First epoch, e.g. 2025-01-01T00:00:00\n"
         "  --epochs <n>       Number of epochs\n"
         "  --interval <sec>   Seconds between epochs\n"
         "  --verbose          Sparse progress\n"
         "  -h, --help         This message\n";
}

/// A satellite id is one system letter followed by two digits, e.g. "C01".
///
/// Checked before constructing a SatID so that a typo in the list is reported
/// rather than silently dropped: the per-satellite try/catch below is there for
/// missing ephemeris data, and it would swallow a bad id just as quietly.
bool looksLikeSatID(const string &s) {
    if (s.size() != 3) return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0]))) return false;
    return std::isdigit(static_cast<unsigned char>(s[1]))
           && std::isdigit(static_cast<unsigned char>(s[2]));
}

/// "YYYY-MM-DDTHH:MM:SS" -> "YYYYMMDD", for the output file name.
std::string yyyymmdd(const string &iso) {
    int y, mo, d, h, mi;
    double sec;
    if (!parseISO8601(iso, y, mo, d, h, mi, sec)) return "unknown";

    std::ostringstream out;
    out << setfill('0') << setw(4) << y << setw(2) << mo << setw(2) << d;
    return out.str();
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/eph.ini";
    bool haveConfigArg = false;

    string optNav, optSp3, optOutDir, optSats, optStart, optEpochs, optInterval;
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
        else if (a == "--nav")      optNav = needValue("--nav");
        else if (a == "--sp3")      optSp3 = needValue("--sp3");
        else if (a == "--out-dir")  optOutDir = needValue("--out-dir");
        else if (a == "--sats")     optSats = needValue("--sats");
        else if (a == "--start")    optStart = needValue("--start");
        else if (a == "--epochs")   optEpochs = needValue("--epochs");
        else if (a == "--interval") optInterval = needValue("--interval");
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
    if (!optNav.empty())      cfg.navFile = optNav;
    if (!optSp3.empty())      cfg.sp3File = optSp3;
    if (!optOutDir.empty())   cfg.outDir = optOutDir;
    if (!optStart.empty())    cfg.startUTC = optStart;

    try {
        if (!optSats.empty())     cfg.satList = splitList(optSats);
        if (!optEpochs.empty())   cfg.epochCount = std::stoi(optEpochs);
        if (!optInterval.empty()) cfg.interval = std::stod(optInterval);
    } catch (const std::exception &) {
        cerr << "Error: --epochs and --interval must be numeric\n";
        return 2;
    }

    if (cfg.epochCount <= 0) {
        cerr << "Error: epoch count must be positive (got " << cfg.epochCount << ")\n";
        return 2;
    }
    if (cfg.interval <= 0.0) {
        cerr << "Error: interval must be positive (got " << cfg.interval << ")\n";
        return 2;
    }

    // Relative config paths resolve against the project root; an explicit
    // command-line path is used verbatim. See apps/spp_if.cpp for the rule.
    string projectRoot = configDir.empty() ? "." : dirOf(configDir);

    // Named locals, not temporaries: RinexNavStore::loadFile takes a non-const
    // reference.
    string navFile = optNav.empty() ? resolvePath(projectRoot, cfg.navFile) : cfg.navFile;
    string sp3File = optSp3.empty() ? resolvePath(projectRoot, cfg.sp3File) : cfg.sp3File;
    string outDir = optOutDir.empty() ? resolvePath(projectRoot, cfg.outDir) : cfg.outDir;

    // 卫星列表：定时从配置读入，不再是文件级全局量——配置解析出来的值不该是可变的全局状态
    vector<SatID> satList;
    for (const string &name: cfg.satList) {
        if (!looksLikeSatID(name)) {
            cerr << "Error: '" << name << "' is not a satellite id (expected e.g. C01)\n";
            return 2;
        }
        satList.emplace_back(name);
    }
    if (satList.empty()) {
        cerr << "Error: satellite list is empty\n";
        return 2;
    }

    int y, mo, d, h, mi;
    double sec;
    if (!parseISO8601(cfg.startUTC, y, mo, d, h, mi, sec)) {
        cerr << "Error: malformed startUTC '" << cfg.startUTC
             << "' (expected YYYY-MM-DDTHH:MM:SS)\n";
        return 2;
    }

    string csvName = "sat_pos_vel_diff_" + yyyymmdd(cfg.startUTC) + ".csv";
    string csvFile = outDir + "/" + csvName;

    if (optVerbose) {
        cout << "config    : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "nav       : " << navFile << "\n";
        cout << "sp3       : " << sp3File << "\n";
        cout << "outDir    : " << outDir << "\n";
        cout << "start     : " << cfg.startUTC << "\n";
        cout << "epochs    : " << cfg.epochCount << " @ " << cfg.interval << " s\n";
        cout << "sats      : " << cfg.satList.size() << " (";
        for (size_t i = 0; i < satList.size(); ++i) cout << (i ? " " : "") << satList[i];
        cout << ")\n";
    }

    if (!ensureDirectory(outDir)) {
        cerr << "Error: cannot create output directory: " << outDir << "\n";
        return 1;
    }

    // 加载广播星历与精密星历
    RinexNavStore navStore;
    try {
        navStore.loadFile(navFile);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load navigation file: " << navFile << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }

    SP3Store sp3Store;
    try {
        sp3Store.loadSP3File(sp3File);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load precise orbit: " << sp3File << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }

    // ======================
    // 2. 输出CSV文件（用于绘图/结果分析）
    // ======================
    ofstream csv(csvFile);
    if (!csv) {
        cerr << "Error: cannot open solution file: " << csvFile << "\n";
        return 1;
    }
    csv << "epoch,sat,x_diff(m),y_diff(m),z_diff(m),vx_diff(m/s),vy_diff(m/s),vz_diff(m/s)" << endl;
    csv << fixed << setprecision(6);

    // ======================
    // 3. 时间序列：从配置的起始时刻开始，按配置步长走配置的历元数
    //    默认值即 2025-01-01 00:00:00 起、30s 步长、全天 2880 历元
    // ======================
    CivilTime ctStart(y, mo, d, h, mi, sec);
    CommonTime tCur = CivilTime2CommonTime(ctStart);

    long long rowsWritten = 0;

    for (int i = 0; i < cfg.epochCount; ++i) {
        YDSTime yds = CommonTime2YDSTime(tCur);

        // 2880 lines of progress is not progress; the two cycle-slip detectors
        // written most recently use the same sparse form.
        if (optVerbose && (i % 500 == 0))
            cout << "epoch " << i << " / " << cfg.epochCount << endl;

        // 遍历所有测试卫星
        for (auto& sat : satList) {
            try {
                // 广播星历计算（自动区分GEO/IGSO/MEO/GPS）
                Xvt xvtNav = navStore.getXvt(sat, tCur);

                // 精密星历（参考真值）
                Xvt xvtSP3 = sp3Store.getXvt(sat, tCur);

                // 位置、速度误差
                Vector3d dPos = xvtNav.x - xvtSP3.x;
                Vector3d dVel = xvtNav.v - xvtSP3.v;

                // 写入CSV
                csv << yds << ","
                    << sat.toString() << ","
                    << dPos[0] << "," << dPos[1] << "," << dPos[2] << ","
                    << dVel[0] << "," << dVel[1] << "," << dVel[2] << endl;
                rowsWritten++;
            }
            catch (...) {
                // 无星历数据时跳过
                continue;
            }
        }

        // 时间步进
        tCur += cfg.interval;
    }

    csv.close();

    cout << "Epochs processed : " << cfg.epochCount << "\n";
    cout << "Rows written     : " << rowsWritten << "\n";
    cout << "Results saved to : " << csvFile << "\n";

    return 0;
}
