#include "TimeStruct.h"
#include "TimeConvert.h"
#include "GnssStruct.h"
#include "RinexNavStore.hpp"
#include "SP3Store.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <string>

using namespace std;

// 卫星列表：GPS + 北斗 GEO / IGSO / MEO 全覆盖
vector<SatID> satList = {
    // GPS卫星
    SatID("G02"),
    SatID("G15"),
    // 北斗GEO卫星
    SatID("C01"),
    SatID("C05"),
    // 北斗IGSO/MEO卫星
    SatID("C11"),
    SatID("C20")
};

int main() {
    // ======================
    // 1. 文件路径配置
    // ======================
    string navFile = "D:\\GnssLab\\data\\BRDC00IGS_R_20250010000_01D_MN.rnx";
    string sp3File = "D:\\GnssLab\\data\\WUM0MGXFIN_20250010000_01D_05M_ORB.SP3";

    // 加载广播星历与精密星历
    RinexNavStore navStore;
    navStore.loadFile(navFile);

    SP3Store sp3Store;
    sp3Store.loadSP3File(sp3File);

    // ======================
    // 2. 输出CSV文件（用于绘图/结果分析）
    // ======================
    ofstream csv("sat_pos_vel_diff_20250101.csv");
    csv << "epoch,sat,x_diff(m),y_diff(m),z_diff(m),vx_diff(m/s),vy_diff(m/s),vz_diff(m/s)" << endl;
    csv << fixed << setprecision(6);

    // ======================
    // 3. 时间序列：2025-01-01 00:00:00 开始，30s步长，全天2880历元
    // ======================
    CivilTime ctStart(2025, 1, 1, 0, 0, 0.0);
    CommonTime tCur = CivilTime2CommonTime(ctStart);

    for (int i = 0; i < 2880; ++i) {
        YDSTime yds = CommonTime2YDSTime(tCur);
        cout << "Processing epoch: " << yds << endl;

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
            }
            catch (...) {
                // 无星历数据时跳过
                continue;
            }
        }

        // 时间步进 30s
        tCur += 30.0;
    }

    csv.close();
    cout << endl;
    cout << "All calculations completed!" << endl;
    cout << "Results saved to: sat_pos_vel_diff_20250101.csv" << endl;

    return 0;
}