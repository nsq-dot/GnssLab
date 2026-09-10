#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <iomanip>
#include <set>
#include <cmath>
#include "GnssStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "RinexNavStore.hpp"
#include "RinexObsReader.h"

#define debug 1

using namespace std;

int main() {

    //--------------------
    // 打开文件流
    //--------------------

    // Replace with your actual RINEX file path
    string dirPath = "D:\\GnssLab\\data\\";

    // rover obs file name
    std::string roverFile = dirPath + "WUH200CHN_R_20250010000_01D_30S_MO.rnx";
    cout << roverFile << endl;

    // nav file name, download from IGS ftp site:ftp://gssc.esa.int/gnss/data/daily/YYYY/brdc
    std::string navFile = dirPath + "BRDC00IGS_R_20250010000_01D_MN.rnx";

    // 2022 03 03 06 48 37.0000000  0 45
    CivilTime stopCivilTime = CivilTime(2025, 1, 1, 1, 0, 30);
    CommonTime stopEpoch = CivilTime2CommonTime(stopCivilTime);

    std::fstream roverObsStream(roverFile);
    if (!roverObsStream) {
        cerr << "rover file open error!" << strerror(errno) << endl;
        exit(-1);
    }

    // read nav file data before rtk
    RinexNavStore navStore;
    navStore.loadFile(navFile);

    cout << "after NavStore" << endl;

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

    ofstream csv("GPS_TGD_Result.csv");
    csv << "Epoch,Sat,Raw_C1,TGD_Sec,TGD_Corr_M,Corrected_C1" << endl;
    csv << fixed << setprecision(6);

    //========新增北斗文件========
    ofstream csv_bds("BDS_TGD_Result.csv");
    csv_bds << "Epoch,Sat,Raw_P,TGD_Sec,TGD_Corr_M,Corrected_P,FreqType" << endl;
    csv_bds << fixed << setprecision(6);

    // 位置1：main开头
    ofstream pl_compare("PL_IonoCompare.csv");
    pl_compare << "TimeOfDay(s),Sat,Raw_PL(m),Corr_PL(m),Elev(deg),Iono_Model(m)" << endl;
    pl_compare << fixed << setprecision(4);

    ofstream f_iono_gps("GPS_Iono.txt");
    ofstream f_iono_bds("BDS_Iono.txt");
    ofstream f_trop("trop_delay_result.txt");
    ofstream f_trop_zwd_zhd("trop_zhd_zwd.txt");

    //======== 习题5.1 发射时间验证文件 =========
    ofstream tx_back_verify("tx_backward_verify.csv", std::ios::out | std::ios::trunc);

    // 3. IF - 单频 差分文件
    ofstream tx_if_single_diff("tx_if_single_diff.csv", std::ios::out | std::ios::trunc);

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

        if (debug) {
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

        if (debug) {
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

        if (debug) {
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
            if (debug)
                cout << "computeElevAzim" << endl;

            computeElevAzim(xyz, satXvtRecTime, satElevData, satAzimData);

            if (debug) {
                cout << "satElevData:" << endl;
                cout << satElevData << endl;
            }

            // todo:
            // computeIonoDelay();
            auto ionoMap = ionoDelay(xyz, epoch, satElevData, satAzimData, navStore);

            for (auto& pair : ionoMap) {
                SatID sat = pair.first;
                double iono = pair.second;
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
                cout << "[TROP] " << sat
                     << "  Elev: " << satElevData[sat] << " deg"
                     << "  Delay: " << trop << " m" << endl;

                // 写入文件
                f_trop << fixed << setprecision(6)<< sod << " "<< sat << " "<< satElevData[sat] << " "<< trop << endl;
            }

        }

        // 调试代码时，设置一个stopEpoch，有助于快速得到结果
        if (roverData.epoch > stopEpoch)
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

    cout << endl;

}

