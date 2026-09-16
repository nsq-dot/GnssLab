/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Systematic-error diagnostics (chapter 5.1): the broadcast TGD correction for
 * GPS and BeiDou, the Klobuchar ionospheric delay, the tropospheric delay, and
 * the transmit-time verification exercises. Results are written as CSV and text
 * files; see the output list below.
 *
 * Usage:
 *   system_bias [config.ini] [options]
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
 *   --stop <ISO8601>      Stop after this epoch, e.g. 2025-01-01T01:00:30
 *   --verbose             Per-epoch and per-satellite detail on stdout. Without
 *                         it the run prints only the closing summary - every
 *                         number it would show also goes to one of the files
 *                         below.
 *   -h, --help            This message
 *
 * Output files, all written into the output directory:
 *   GPS_TGD_Result.csv    GPS C1 TGD correction
 *   BDS_TGD_Result.csv    BeiDou B1I/B2I TGD correction
 *   PL_IonoCompare.csv    pseudorange-minus-phase against the model ionosphere
 *   GPS_Iono.txt          GPS ionospheric delay per satellite and epoch
 *   BDS_Iono.txt          BeiDou ionospheric delay per satellite and epoch
 *   trop_delay_result.txt tropospheric delay per satellite and epoch
 *   trop_zhd_zwd.txt      its zenith hydrostatic and wet components
 *   tx_backward_verify.csv  transmit-time back-verification (exercise 5.1)
 *   tx_if_single_diff.csv   ionosphere-free minus single-frequency transmit time
 */

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <cmath>

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
         "Broadcast TGD, ionospheric and tropospheric diagnostics.\n"
         "\n"
         "  config.ini         Configuration file (default: config/bias.ini)\n"
         "\n"
         "Options:\n"
         "  --obs <file>       RINEX observation file\n"
         "  --nav <file>       RINEX broadcast navigation file\n"
         "  --out-dir <dir>    Output directory\n"
         "  --stop <ISO8601>   Stop after this epoch, e.g. 2025-01-01T01:00:30\n"
         "  --verbose          Per-epoch and per-satellite detail\n"
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

    // read nav file data before rtk
    RinexNavStore navStore;
    try {
        navStore.loadFile(navFile);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load navigation file: " << navFile << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }

    std::map<string, std::set<string>> selectedTypes;
    selectedTypes["G"].insert("C1C");
    selectedTypes["G"].insert("C2W");
    selectedTypes["G"].insert("L1C");
    selectedTypes["G"].insert("L2W");

    // 北斗 BDS (C)
    selectedTypes["C"].insert("C1X");
    selectedTypes["C"].insert("C2I");
    selectedTypes["C"].insert("C7I");
    selectedTypes["C"].insert("L1X");
    selectedTypes["C"].insert("L2I");
    selectedTypes["C"].insert("L7I");

    //-------------------
    // 定义数据处理的对象
    //-------------------
    //>>> classes for rover
    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    ofstream csv(outDir + "/GPS_TGD_Result.csv");
    csv << "Epoch,Sat,Raw_C1,TGD_Sec,TGD_Corr_M,Corrected_C1" << endl;
    csv << fixed << setprecision(6);

    //========新增北斗文件========
    ofstream csv_bds(outDir + "/BDS_TGD_Result.csv");
    csv_bds << "Epoch,Sat,Raw_P,TGD_Sec,TGD_Corr_M,Corrected_P,FreqType" << endl;
    csv_bds << fixed << setprecision(6);

    // 位置1：main开头
    ofstream pl_compare(outDir + "/PL_IonoCompare.csv");
    pl_compare << "TimeOfDay(s),Sat,Raw_PL(m),Corr_PL(m),Elev(deg),Iono_Model(m)" << endl;
    pl_compare << fixed << setprecision(4);

    ofstream f_iono_gps(outDir + "/GPS_Iono.txt");
    ofstream f_iono_bds(outDir + "/BDS_Iono.txt");
    ofstream f_trop(outDir + "/trop_delay_result.txt");
    ofstream f_trop_zwd_zhd(outDir + "/trop_zhd_zwd.txt");

    //======== 习题5.1 发射时间验证文件 =========
    ofstream tx_back_verify(outDir + "/tx_backward_verify.csv", std::ios::out | std::ios::trunc);

    // 3. IF - 单频 差分文件
    ofstream tx_if_single_diff(outDir + "/tx_if_single_diff.csv", std::ios::out | std::ios::trunc);

    // Fail loudly rather than running the whole solve and writing nothing.
    for (const auto &stream: {&csv, &csv_bds, &pl_compare, &f_iono_gps, &f_iono_bds,
                              &f_trop, &f_trop_zwd_zhd, &tx_back_verify, &tx_if_single_diff}) {
        if (!*stream) {
            cerr << "Error: cannot open an output file in " << outDir << "\n";
            return 1;
        }
    }

    int epochCount = 0;

    while (true) {

        // solve spp for rover
        ObsData roverData;

        try {
            roverData = readObsRover.parseRinexObs();
        }
        catch (EndOfFile &e) { break; }

        CommonTime epoch = roverData.epoch;

        //----------------------
        // 去掉通道号，C1W, C1C => C1;
        // 后面computeSatPos里用与通道号无关的观测值计算卫星发射时刻位置
        //----------------------
        convertObsType(roverData);

        YDSTime ydst = CommonTime2YDSTime(epoch);
        double sod = ydst.sod;

        if (optVerbose) {
            cout << "after convertObsType" << endl;
            cout << roverData << endl;
        }

        //----------------------
        // 得到卫星发射时刻位置和钟差、相对论和TGD后，改正观测值延迟，并更新C1/C2等观测值
        //----------------------
        // todo:
        //correctTGD(obsData);

        const double C_MPS = 299792458.0;

        for (auto& stv : roverData.satTypeValueData) {
            SatID sat = stv.first;
            auto& obs = stv.second;

            try {
                double P_raw = 0.0;
                double TGD = 0.0;
                bool valid = false;

                // ==========================================
                // GPS 卫星 C1
                // ==========================================
                if (sat.system == "G" && obs.count("C1"))
                {
                    NavEphGPS eph = navStore.findGPSEph(sat, epoch);
                    TGD = eph.TGD;
                    P_raw = obs["C1"];
                    valid = true;
                }

                // ==========================================
                // 北斗 B1I (C2I → C2)
                // ==========================================
                else if (sat.system == "C" && obs.count("C2"))
                {
                    NavEphBDS eph = navStore.findBDSEph(sat, epoch);
                    TGD = eph.TGD1;
                    P_raw = obs["C2"];
                    valid = true;
                }

                // ==========================================
                // 北斗 B2I (C7I → C7)
                // ==========================================
                else if (sat.system == "C" && obs.count("C7"))
                {
                    NavEphBDS eph = navStore.findBDSEph(sat, epoch);
                    TGD = eph.TGD2;
                    P_raw = obs["C7"];
                    valid = true;
                }

                if (!valid) continue;

                // 统一改正公式
                double corr = TGD * C_MPS;
                double P_corr = P_raw - corr;

                //GPS写入原csv
                if(sat.system == "G")
                {
                    csv << CommonTime2YDSTime(epoch) << ","<< sat.toString() << ","
                        << P_raw << ","<< TGD << ","<< corr << ","<< P_corr << endl;
                }
                //北斗写入单独BDS csv，标注B1/B2频点
                else if(sat.system == "C")
                {
                    string freqMark;
                    if(obs.count("C2")) freqMark = "B1I(C2)";
                    else if(obs.count("C7")) freqMark = "B2I(C7)";

                    csv_bds << CommonTime2YDSTime(epoch) << ","<< sat.toString() << ","
                            << P_raw << ","<< TGD << ","
                            << corr << ","<< P_corr << ","<< freqMark << endl;
                }

                if (optVerbose)
                    cout << "[TGD] " << sat << " | RAW: " << P_raw
                         << " | CORR: " << corr << " | NEW: " << P_corr << endl;

            } catch (...) {
                continue;
            }
        }

        // ===================== 1. 计算 IF 电离层无关组合伪距 =====================
        std::map<SatID, double> ifPRMap = computeIFPseudorange(roverData, navStore);

        // ===================== 2. 两路求解卫星发射时刻状态 =====================
        // ① 单频非组合伪距
        std::map<SatID, Xvt> satXvtTransTime = computeSatPos(roverData, navStore);

        // ② IF组合伪距
        std::map<SatID, Xvt> satXvtTransTime_IF = computeSatPos(roverData, navStore, &ifPRMap);

        if (optVerbose) {
            cout << "satXvtTransTime" << CommonTime2CivilTime(roverData.epoch) << endl;
            for (auto sx: satXvtTransTime) {
                cout << sx.first << endl;
                cout << sx.second << endl;
            };
        }

        Vector3d xyz = roverData.antennaPosition;

        // -------- 单频链路：自转改正 + 写入 --------
        std::map<SatID, Xvt> satXvtRecTime = earthRotation(xyz,satXvtTransTime,roverData.epoch);
        verifyTransmitTime(roverData.epoch, xyz, satXvtRecTime, tx_back_verify);

        // ========== IF 链路缺失部分 ==========
        // Step3：IF 数据做地球自转改正
        std::map<SatID, Xvt> satXvtRecTime_IF = earthRotation(xyz, satXvtTransTime_IF, roverData.epoch);

        // 反向验证发射时刻
        verifyTransmitTime(roverData.epoch, xyz, satXvtRecTime, tx_back_verify);

        verifyTransmitTimeCompare(roverData.epoch, xyz, satXvtRecTime, satXvtRecTime_IF, tx_if_single_diff);

        if (optVerbose) {
            cout << "satXvtRecTime " << endl;
            for (auto sx: satXvtRecTime) {
                cout << sx.first << " xvt:" << endl;
                cout << sx.second << endl;
            };
        }

        SatValueMap satElevData, satAzimData;
        // 地球表面才计算高度角和大气改正
        if (std::abs(xyz.norm() - RadiusEarth) < 100000.0) {
            satElevData.clear();
            satAzimData.clear();
            if (optVerbose)
                cout << "computeElevAzim" << endl;

            computeElevAzim(xyz, satXvtRecTime, satElevData, satAzimData);

            if (optVerbose) {
                cout << "satElevData:" << endl;
                cout << satElevData << endl;
            }

            // todo:
            // computeIonoDelay();
            auto ionoMap = ionoDelay(xyz, epoch, satElevData, satAzimData, navStore);

            for (auto& pair : ionoMap) {
                SatID sat = pair.first;
                double iono = pair.second;
                if (optVerbose)
                    cout << "[IONO] " << sat
                         << "  Elev: " << satElevData[sat] << " deg"
                         << "  Iono: " << iono << " m" << endl;

                if(sat.system == "G"){
                    f_iono_gps << fixed << setprecision(6)
                           << sod << " "
                           << sat << " "
                           << satElevData[sat] << " "
                           << iono << endl;
                }
                else if(sat.system == "C"){
                    f_iono_bds << fixed << setprecision(6)
                           << sod << " "
                           << sat << " "
                           << satElevData[sat] << " "
                           << iono << endl;
                }

                try {
                    double Praw = 0.0, Lraw = 0.0;
                    auto& obsMap = roverData.satTypeValueData.at(sat);

                    if (sat.system == "G") {
                        // GPS 缩写后 C1 / L1
                        if (!obsMap.count("C1") || !obsMap.count("L1"))
                            continue;
                        Praw = obsMap["C1"];
                        Lraw = obsMap["L1"];
                    }
                    else if (sat.system == "C") {
                        // 北斗：伪距 C2，载波被缩写为 L2
                        if (!obsMap.count("C2") || !obsMap.count("L2"))
                            continue;
                        Praw = obsMap["C2"];
                        Lraw = obsMap["L2"];
                    }
                    else {
                        continue;
                    }

                    double PL_raw   = Praw - Lraw;
                    double PL_corr  = (Praw - iono) - Lraw;
                    double elev     = satElevData[sat];

                    pl_compare << sod << "," << sat.toString() << ","
                               << PL_raw << "," << PL_corr << "," << elev << "," << iono << endl;
                }
                catch (...) {
                    continue;
                }

                // 写入文件
            }

            // computeTropDealy();
            auto tropMap = tropDelay(xyz, satElevData, satAzimData, sod, f_trop_zwd_zhd);

            for (auto& pair : tropMap) {
                SatID sat = pair.first;
                double trop = pair.second;
                if (optVerbose)
                    cout << "[TROP] " << sat
                         << "  Elev: " << satElevData[sat] << " deg"
                         << "  Delay: " << trop << " m" << endl;

                // 写入文件
                f_trop << fixed << setprecision(6)<< sod << " "<< sat << " "<< satElevData[sat] << " "<< trop << endl;
            }

        }

        // Counted before the stop test: the epoch that trips the test has already
        // been fully processed and written, so it belongs in the total.
        epochCount++;

        // 调试代码时，设置一个stopEpoch，有助于快速得到结果
        if (haveStop && roverData.epoch > stopEpoch)
            break;
    }
    csv.close();
    f_iono_gps.close();
    f_iono_bds.close();
    f_trop.close();
    f_trop_zwd_zhd.close();
    tx_back_verify.close();
    tx_if_single_diff.close();
    pl_compare.close();
    roverObsStream.close();

    cout << "Epochs processed : " << epochCount << "\n";
    cout << "Output written to " << outDir << "\n";

    return 0;
}

