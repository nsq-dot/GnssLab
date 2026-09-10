#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <set>
#include <map>
#include "GnssStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "RinexNavStore.hpp"
#include "RinexObsReader.h"
#include "SPPIFCode.h"

#define debug 1

using namespace std;

int main() {

    //--------------------
    // 打开文件流
    //--------------------

    // Replace with your actual RINEX file path
    string dirPath = "D:/GnssLab/data/";
//    string dirPath = "D:\\documents\\Source\\gnssLab-2.1\\data\\";

    // rover obs file name
    std::string roverFile = dirPath + "WUH200CHN_R_20250010000_01D_30S_MO.rnx";
    //std::string roverFile = dirPath + "ABMF00GLP_R_20210010000_01D_30S_MO.rnx";

    cout << roverFile << endl;

    // nav file name, download from IGS ftp site:ftp://gssc.esa.int/gnss/data/daily/YYYY/brdc
    std::string navFile = dirPath + "BRDC00IGS_R_20250010000_01D_MN.rnx";
    // std::string navFile = dirPath + "ABMF00GLP_R_20210010000_01D_MN.rnx";

    // 2025 01 01 23 59 30.0000000  0 60
    CivilTime stopCivilTime = CivilTime(2025, 01, 01, 0, 0, 30);
    //   CivilTime stopCivilTime = CivilTime(2025, 01, 01, 23, 59, 30);

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

    std::map<string, std::pair<string, string>> ifCodeTypes;
    ifCodeTypes["G"].first = "C1";
    ifCodeTypes["G"].second = "C2";

    //-------------------
    // 定义数据处理的对象
    //-------------------
    //>>> classes for rover
    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    std::string solFile = roverFile + ".spp.out";
    if(debug)
        cout << solFile << endl;

    // ====================== 全局统计变量 ======================
    int total_epoch = 0;  // 总历元数

    // 各系统卫星集合（自动去重）
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
            cout << "roverData:" << roverData << endl;

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

        if (roverData.epoch > stopEpoch)
            break;
    }

    roverObsStream.close();

    // ====================== 输出 CSV 文件用于 Python 绘图 ======================
    ofstream csvFile("GNSS_Statistics.csv");
    csvFile << "Epoch,GPS,BDS,Galileo,GLONASS,Total\n";
    for (int i = 0; i < epoch_list.size(); i++) {
        csvFile << epoch_list[i] << ","
                << gps_count_list[i] << ","
                << bds_count_list[i] << ","
                << gal_count_list[i] << ","
                << glo_count_list[i] << ","
                << total_sat_list[i] << "\n";
    }
    csvFile.close();

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


    navStore.loadFile(navFile);
    navStore.printAllGPSNav();

}
