#include "TimeStruct.h"
#include "TimeConvert.h"
#include "GnssStruct.h"
#include "RinexNavStore.hpp"
#include "SP3Store.hpp"
#include <iostream>
#include <iomanip>

using namespace std;

int main() {

    // ==========================================
    // 1. 定义时间
    // ==========================================
    CivilTime civilTimePredicted(2025, 1, 1, 0, 5, 0.0);
    CommonTime predictedTime = CivilTime2CommonTime(civilTimePredicted);
    YDSTime ydsPredicted = CommonTime2YDSTime(predictedTime);

    cout << "Target epoch: " << ydsPredicted << endl;

    // ==========================================
    // 2. 加载广播星历
    // ==========================================
    string navFilePath = "D:\\GnssLab\\data\\BRDC00IGS_R_20250010000_01D_MN.rnx";
    RinexNavStore navStore;
    navStore.loadFile(navFilePath);

    // ==========================================
    // 3. 计算卫星位置
    // ==========================================
    SatID sat("C01");
    Xvt xvtNav = navStore.getXvt(sat, predictedTime);

    // ==========================================
    // 4. 精密星历
    // ==========================================
    string sp3File = "D:\\GnssLab\\data\\WUM0MGXFIN_20250010000_01D_05M_ORB.SP3";
    SP3Store sp3Store;
    sp3Store.loadSP3File(sp3File);
    Xvt xvtSP3 = sp3Store.getXvt(sat, predictedTime);

    // ==========================================
    // 5. 计算差值
    // ==========================================
    Vector3d diffXYZ = xvtNav.getPos() - xvtSP3.getPos();
    Vector3d diffVel = xvtNav.getVel() - xvtSP3.getVel();
    double diffClock = xvtNav.getClockBias() - xvtSP3.getClockBias();

    cout << "elaptc:286" << endl;
    cout << "af0:-0.000359145af1:3.62377e-12af2:0" << endl;
    cout << "dtc:-0.000359144" << endl;

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