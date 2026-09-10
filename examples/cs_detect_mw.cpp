#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <set>
#include "GnssStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"

using namespace std;

int main() {

    string dirPath = "D:\\GnssLab\\data\\Zero-baseline";

    // Replace with your actual RINEX file path
    std::string rinexFile
            = dirPath + "oem719-202203031500-1.obs";

    cout << rinexFile << endl;

    std::fstream rinexFileStream(rinexFile);
    if (!rinexFileStream) {
        cerr << "rinex file open error!" << strerror(errno) << endl;
        exit(-1);
    }

//    RinexHeader rinexHeader;
//    parseRinexHeader(rinexFileStream,rinexHeader);

    CivilTime stopCivilTime = CivilTime(2022, 03, 03, 07, 00, 00);
    CommonTime stopEpoch = CivilTime2CommonTime(stopCivilTime);

    std::map<string, std::set<string>> sysTypes;
    sysTypes["G"].insert("C1C");
    sysTypes["G"].insert("C2W");
    sysTypes["G"].insert("L1C");
    sysTypes["G"].insert("L2W");

    sysTypes["C"].insert("C2I");
    sysTypes["C"].insert("C6I");
    sysTypes["C"].insert("L2I");
    sysTypes["C"].insert("L6I");

    // 把所有历元的MW/meanMW存储到一个数据结构中，用于分析
    SatEpochValueMap satEpochMWData;
    SatEpochValueMap satEpochMeanMWData;
    SatEpochValueMap satEpochCSFlagData;

    while (true) {
        ObsData obsData;

        // Read data from RINEX file
        try {
            obsData = parseRinexObs(rinexFileStream);

        }
        catch (...) {
            break;
        }

        // 调试代码时，设置一个stopEpoch，有助于快速得到结果
        if (obsData.epoch > stopEpoch)
            break;

        // choose obs types
        chooseObs(obsData, sysTypes);

        cout << "chooseObs:" << endl;
        cout << obsData << endl;

        convertObsType(obsData);
        cout << "convertObsType:" << endl;
        cout << obsData << endl;

        // Detect cycle slips using MW combination
        std::map<Variable, int> csFlagData;
        detectCSMW(obsData,
                   csFlagData,
                   satEpochMWData,
                   satEpochMeanMWData,
                   satEpochCSFlagData);

    }

    //=============================
    // 处理完所有历元后，输出一些结果
    //=============================

    // 为每颗卫星创建一个文件
    std::vector<std::fstream> satelliteStreams;
    for (auto sd: satEpochMWData) {
        string satFile = dirPath + sd.first.toString();
        cout << satFile << endl;
        std::fstream satStream(satFile, std::ios::out);
        if (!satStream.is_open()) {
            cerr << "open file for sat error!" << endl;
            exit(-1);
        }
        satelliteStreams.push_back(std::move(satStream)); // 将文件流移动到vector中
    }

    // 逐个卫星输出每个历元的MW数值和meanMW数值；
    int i(0);
    for (auto sd: satEpochMWData) {
        for (auto ed: sd.second) {
            satelliteStreams[i]
                    << sd.first << " "
                    << CommonTime2YDSTime(ed.first) << " "
                    << std::fixed << setprecision(3)
                    << ed.second << " "
                    << satEpochMeanMWData[sd.first][ed.first] << " "
                    << satEpochCSFlagData[sd.first][ed.first] << " "
                    << endl;
        }
        i++;
    }

    // 循环结束后，关闭所有打开的文件流
    for (auto &stream: satelliteStreams) {
        stream.close(); // 关闭文件
    }

    rinexFileStream.close();

    return 0;
}