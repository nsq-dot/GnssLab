#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <set>
#include "GnssStruct.h"
#include "CoordConvert.h"
#include "CoordStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "RinexNavStore.hpp"
#include "RinexObsReader.h"
#include "SPPIFCode.h"
#include "SPPVelocity.h"

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
    //std::string roverFile = dirPath + "ABMF00GLP_R_20210010000_01D_30S_MO.rnx";

    cout << roverFile << endl;

    // nav file name, download from IGS ftp site:ftp://gssc.esa.int/gnss/data/daily/YYYY/brdc
    std::string navFile = dirPath + "BRDC00IGS_R_20250010000_01D_MN.rnx";
   // std::string navFile = dirPath + "ABMF00GLP_R_20210010000_01D_MN.rnx";

    // 2022 03 03 06 48 37.0000000  0 45
    CivilTime stopCivilTime = CivilTime(2025, 01, 01, 0,30 , 30);
    //   CivilTime stopCivilTime = CivilTime(2021, 01, 01, 10, 00 , 00);

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

    std::map<string, std::pair<string, string>> ifCodeTypes;
    // GPS：完整伪距标识 C1C / C2W
    ifCodeTypes["G"] = std::make_pair("C1", "C2");
    // 北斗：完整伪距标识 C2I / C7I
    // B1I(C2) + B3I(C6)，对应多普勒 D2I、D6I
    ifCodeTypes["C"] = std::make_pair("C2", "C6");

    //-------------------
    // 定义数据处理的对象
    //-------------------
    //>>> classes for rover
    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    SPPIFCode sppif;
    sppif.setRinexNavStore(&navStore);
    sppif.setIFCodeTypes(ifCodeTypes);

    // 实验控制开关，按需修改切换对照组
    bool runOnlyGPS = false;
    bool runOnlyBDS = false;
    bool runDualSys = true;


    bool closeTrop = false;    // true=关闭对流层，做误差对比
    bool closeBDSTGD = false;  // true=关闭北斗TGD，做误差对比

    // 传入解算类
    sppif.setTropEnable(!closeTrop);
    sppif.setBDSTGDEnable(!closeBDSTGD);

    // ========== 新增切换定位模式 ==========
    sppif.setSolveMode(SPPIFCode::DUAL_IF_COMB);

    //>>> classes for rtk;
    SolverLSQ solver;

    //std::string solFile = roverFile + "_GPS_OnTrop.spp.out";
    //std::string solFile = roverFile + "_GPS_OffTrop.spp.out";
    //std:: string solFile = roverFile + "_BDS_OnTGD.spp.out";
    //std::string solFile = roverFile + "_BDS_OffTGD.spp.out";
    //std::string solFile = roverFile + "_DUAL_GPSBDS_AllCorr.spp.out";

    //双频 IF 组合
    std::string solFile = roverFile + "_DUAL_IF.spp.out";
    //双频非组合 DUAL_RAW
    //std::string solFile = roverFile + "_DUAL_Raw.spp.out";

    if(debug)
        cout << solFile << endl;

    std::fstream solStream(solFile, ios::out);
    if (!solStream) {
        cerr << "solution file open error!" << strerror(errno) << endl;
        exit(-1);
    }
    // 新增：定位+测速联合输出文件
    string velFileName = roverFile + "_pos_vel.out";
    std::ofstream velOutFile(velFileName, ios::trunc);
    velOutFile << fixed << setprecision(6);
    velOutFile << "sod X Y Z vE vN vU recClkDot\n";

    while (true) {
        ObsData roverData;
        try {
            roverData = readObsRover.parseRinexObs();
        }
        catch (EndOfFile &e) {
            break;
        }

        // 卫星筛选逻辑不变
        SatIDSet delSat;
        for(auto &stv : roverData.satTypeValueData)
        {
            string sys = stv.first.system;
            if(runOnlyBDS && sys != "C") delSat.insert(stv.first);
            if(runOnlyGPS && sys != "G") delSat.insert(stv.first);
        }
        for(auto sat : delSat)
            roverData.satTypeValueData.erase(sat);

        CommonTime epoch = roverData.epoch;

        // ========== 修复CommonTime2YDSTime函数调用 ==========
        YDSTime ydst = CommonTime2YDSTime(epoch);
        double sod = ydst.sod;

        // ========== 修正坐标类型：Eigen::Vector3d 匹配SPPIF内部xyz ==========
        XYZ posXYZ;
        Eigen::Vector3d velXYZ(0,0,0);
        double recClkDot = 0.0;
        bool velOk = sppif.runPosVel(roverData, posXYZ, velXYZ, recClkDot);

        // 输出定位结果
        Vector3d xyzRover = sppif.getXYZ();
        printSolution(solStream, epoch, xyzRover);

        // 测速ENU转换+写入文件
        Eigen::Vector3d velENU(0,0,0);
        if (velOk)
        {
            velENU = sppif.getVelObj().xyz2Enu(posXYZ, velXYZ);
            std::cout << "[Vel] E/N/U:" << velENU.transpose() << " clkDot:" << recClkDot << std::endl;
        }
        // ========== 移除钟差recClk相关全部代码，不再输出该列 ==========
        velOutFile << std::fixed << std::setprecision(6)
                   << sod << " "
                   << posXYZ[0] << " " << posXYZ[1] << " " << posXYZ[2] << " "
                   << velENU[0] << " " << velENU[1] << " " << velENU[2] << " "
                   << recClkDot << "\n";

        if (roverData.epoch > stopEpoch)
            break;
    }

    roverObsStream.close();
    solStream.close();
    velOutFile.close();
    return 0;
}


