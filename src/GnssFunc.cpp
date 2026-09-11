#include <string>
#include <algorithm> //replace 函数
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include "TimeConvert.h"
#include "GnssStruct.h"
#include "GnssFunc.h"
#include "ARLambda.hpp"
#include "CoordConvert.h"
#include "RinexNavStore.hpp"

// Diagnostics are off by default. These drive per-observation, per-satellite
// printing inside the epoch loop, which is unusable on a multi-hour 1 Hz file
// (the zero-baseline set is ~8000 epochs) and drowns out any run you want to
// read numbers from. Turn them on per-target with e.g.
//     -DGNSSLAB_DEBUG_PARSER=1 -DGNSSLAB_DEBUG_CSMW=1
// Only stdout is affected, never the numeric output the regression baseline
// compares, so flipping these cannot move a baseline.
#ifndef GNSSLAB_DEBUG_PARSER
#define GNSSLAB_DEBUG_PARSER 0
#endif
#ifndef GNSSLAB_DEBUG_CSMW
#define GNSSLAB_DEBUG_CSMW 0
#endif

// CoordConvert.h 已经给 debug 提供了默认值（它自己的内联函数要用），
// 这里必须先撤销再重定义，否则是宏重定义。
#undef debug
#define debug GNSSLAB_DEBUG_PARSER
#define debugCSMW GNSSLAB_DEBUG_CSMW

void parseRinexHeader(std::fstream &rinexFileStream, RinexHeader &rinexHeader) {

    double version;
    XYZ antennaPosition;
    string satSys;
    std::map<string, std::vector<string>> mapObsTypes;
    // Declared observation-type count per constellation, remembered across the
    // continuation records of SYS / # / OBS TYPES.
    std::map<string, int> numObsBySystem;
    while (true) {
        string line;
        getline(rinexFileStream, line);

        if (debug) {
            cout << "parseRinexHeader:" << line << endl;
        }

        // Running off the end without an END OF HEADER record is a malformed
        // file. Without this the loop below reads empty lines forever, which
        // looks like a hang rather than a parse failure.
        if (line.empty() && rinexFileStream.eof()) {
            FFStreamError e("RINEX header ended without an END OF HEADER record");
            throw e;
        }

        // Take whatever columns 60..79 hold and strip the padding, without
        // requiring a full 80-character record - this is what RinexObsReader
        // does, and the two must agree because both read the same files.
        //
        // The guard is load-bearing in the other direction too: a file whose
        // trailing whitespace has been stripped (scripts/make_sample_data.py
        // produces exactly that, and it is the committed sample) ends its
        // "END OF HEADER" record at column 72, so a `size() >= 80` test would
        // never extract a label and the header would never terminate.
        string label;
        if (line.size() > 60)
            label = strip(line.substr(60, 20));

        if (label == "END OF HEADER") {
            break;
        } else if (label == "MARKER NAME") {
            string markerName = line.substr(0, 60);
            std::replace(markerName.begin(), markerName.end(), ' ', '_');
            rinexHeader.station = markerName;

        } else if (label == "RINEX VERSION / TYPE") {
            version = safeStod(line.substr(0, 20));
            if (version < 3.04 || version > 3.05) {
                cerr << "only support rinex 3.04/3.05 version!" << endl;
                exit(-1);
            }
            rinexHeader.version = version;
        } else if (label == "APPROX POSITION XYZ") {
            antennaPosition[0] = safeStod(line.substr(0, 14));
            antennaPosition[1] = safeStod(line.substr(14, 14));
            antennaPosition[2] = safeStod(line.substr(28, 14));
            rinexHeader.antennaPosition = antennaPosition;
        } else if (label == "SYS / # / OBS TYPES") {
            // Same discarded-strip bug as `label` above: a continuation record
            // starts with a space, which must strip down to "" so the count is
            // not re-read from a line that does not carry one.
            string sysStr = strip(line.substr(0, 1));

            if (sysStr != "") {
                satSys = sysStr;

                // The declared count appears only on the first record for a
                // constellation; a constellation with more than 13 types
                // continues on further records that leave the system character
                // blank. Remember the count so the continuation records keep
                // filling the same list.
                //
                // This used to be an uninitialised local, read on the
                // continuation path - indeterminate, and in practice the loop
                // condition was false, so every type past the 13th was silently
                // dropped. On the committed sample that loses GPS L2W and all of
                // the BeiDou phase, leaving no satellite that can form the
                // combination at all.
                numObsBySystem[satSys] = stoi(line.substr(3, 3));
            }

            const int maxObsPerLine = 13;
            int target = numObsBySystem.count(satSys) ? numObsBySystem[satSys] : 0;

            for (int i = 0; i < maxObsPerLine && (int) mapObsTypes[satSys].size() < target; i++) {
                size_t start = 4 * i + 7;
                if (start + 3 > line.size())
                    break;
                std::string typeStr = strip(line.substr(start, 3));
                if (typeStr.empty())
                    break;
                // insert into mapObsTypes
                mapObsTypes[satSys].push_back(typeStr);
            }
            rinexHeader.mapObsTypes = mapObsTypes;
        }
    }
};

ObsData parseRinexObs(std::fstream &rinexFileStream) {
    static bool isHeaderRead = false;
    static RinexHeader rinexHeader;

    if (!isHeaderRead) {
        parseRinexHeader(rinexFileStream, rinexHeader);
        isHeaderRead = true;
    }

    // 读取观测值
    std::string line;
    getline(rinexFileStream, line);

    if (rinexFileStream.eof()) {
        EndOfFile err("EOF encountered!");
        throw err;
    }

    if (debug) {
        std::cout << "current record line is:" << std::endl;
        std::cout << line << std::endl;
    }

    // 检查并解析历元行
    // 检查历元标记 ('>') 和随后的空格。
    if (line[0] != '>' || line[1] != ' ') {
        FFStreamError e("Bad epoch line: >" + line + "<");
        throw e;
    }

    int epochFlag = stoi(line.substr(31, 1));
    if (epochFlag < 0 || epochFlag > 6) {
        FFStreamError e("Invalid epoch flag: " + std::to_string(epochFlag));
        throw e;
    }

    CommonTime currEpoch = parseTime(line);
    if (debug) {
        std::cout << " currEpoch" << currEpoch << std::endl;
    }

    int numSats = stoi(line.substr(32, 3));

    if (debug) cout << numSats << endl;

    // 读取观测：SV ID 和数据
    SatTypeValueMap stvData;
    if (epochFlag == 0 || epochFlag == 1 || epochFlag == 6) {

        std::vector<SatID> satIndex(numSats);
        for (int isv = 0; isv < numSats; ++isv) {
            getline(rinexFileStream, line); // 修改了这里的变量名以匹配上下文

            if (debug) {
                cout << "parseRinexObs:" << line << endl;
            }

            if (rinexFileStream.eof()) {
                EndOfFile err("EOF encountered!");
                throw err;
            }

            // 获取 SV ID
            try {
                satIndex[isv] = SatID(line.substr(0, 3));
            } catch (std::exception &e) {
                FFStreamError ffse(e.what());
                throw ffse;
            }

            SatID sat = SatID(satIndex[isv]);

            // 如果卫星系统不是GPS("G")也不是北斗("C")，则跳过当前循环迭代。
            if (sat.system.empty()) {
                continue;
            }

            // 检查是否有观测类型定义
            if (rinexHeader.mapObsTypes.count(sat.system) == 0) {
                continue;
            }

            int size = rinexHeader.mapObsTypes.at(satIndex[isv].system).size();

            // 有些文件没有观测值，后面就没有输出，这里用空格来替换，否则解析错误
            size_t minSize = 3 + 16 * size;
            if (line.size() < minSize) {
                line += std::string(minSize - line.size(), ' ');
            }

            // 获取数据 (# entries in ObsType map of maps from header)
            TypeValueMap typeObs;
            TypeValueMap typeLLI;
            TypeValueMap typeSSI;
            for (int i = 0; i < size; ++i) {
                size_t pos = 3 + 16 * i;
                std::string str = line.substr(pos, 16);

                // ObsType
                std::string obsTypeStr = rinexHeader.mapObsTypes.at(sat.system)[i];

                // 观测值
                std::string tmpStr = str.substr(0, 14);

                double data = safeStod(tmpStr);

                // 载波相位
                if (obsTypeStr[0] == 'L') {
                    double wavelength = 0.0;

                    // 获取观测值频率，比如L1C，其频率为1
                    int n;
                    if (obsTypeStr[1] == 'A') {
                        n = 1;
                    } else {
                        n = stoi(obsTypeStr.substr(1, 1));
                    }

                    wavelength = getWavelength(sat.system, n);

                    if (wavelength == 0.0) continue;

                    if (debug) {
                        std::cout << obsTypeStr << " wavelength"
                                  << std::setprecision(12)
                                  << wavelength << std::endl;
                    }

                    // 将周期转换为米
                    data = data * wavelength;
                }

                // 观测值异常
                if (std::abs(data) == 0.0) {
                    continue;
                }

                typeObs[obsTypeStr] = data;
            }

            // 插入当前卫星的数据到 stvData
            stvData[satIndex[isv]] = typeObs;

        }
    }

    ObsData obsData;
    obsData.station = rinexHeader.station;
    obsData.epoch = currEpoch;
    obsData.satTypeValueData = stvData;

    return obsData;
}


CommonTime parseTime(const string &line) {

    // check if the spaces are in the right place - an easy
    // way to check if there's corruption in the file
    if ((line[1] != ' ') || (line[6] != ' ') || (line[9] != ' ') ||
        (line[12] != ' ') || (line[15] != ' ') || (line[18] != ' ') ||
        (line[29] != ' ') || (line[30] != ' ')) {
        FFStreamError e("Invalid time format");
        throw (e);
    }

    // if there's no time, just return a bad time
    if (line.substr(2, 27) == string(27, ' '))
        return BEGINNING_OF_TIME;

    int year, month, day, hour, min;
    double sec;

    year = stoi(line.substr(2, 4));
    month = stoi(line.substr(7, 2));
    day = stoi(line.substr(10, 2));
    hour = stoi(line.substr(13, 2));
    min = stoi(line.substr(16, 2));
    sec = safeStod(line.substr(19, 11));

    // Real Rinex has epochs 'yy mm dd hr 59 60.0' surprisingly often.
    double ds = 0;
    if (sec >= 60.0) {
        ds = sec;
        sec = 0.0;
    }

    CommonTime ctime;
    CivilTime cv = CivilTime(year, month, day, hour, min, sec);
    ctime = CivilTime2CommonTime(cv);

    if (ds != 0)
        ctime = ctime + ds;

    return ctime;


}  // end parseTime

void chooseObs(ObsData &obsData, std::map<std::string, std::set<std::string>> &sysTypes) {
    SatTypeValueMap filteredSatTypeValueData;

    // Iterate over all satellite entries in satTypeValueData
    for (const auto &satEntry: obsData.satTypeValueData) {
        const auto &satId = satEntry.first;
        const auto &typeValueMap = satEntry.second;

        // Check if the satellite's system is in sysTypes
        auto itSys = sysTypes.find(satId.system);
        if (itSys != sysTypes.end()) { // If the system is found in sysTypes
            const auto &allowedTypes = itSys->second;
            TypeValueMap filteredTypeValueMap;

            // Filter the observations based on the allowed types
            for (const auto &typeValueEntry: typeValueMap) {
                if (allowedTypes.find(typeValueEntry.first) != allowedTypes.end()) {
                    filteredTypeValueMap.insert(typeValueEntry);
                }
            }

            // Only add the satellite entry if there are any remaining observations
            if (!filteredTypeValueMap.empty()) {
                filteredSatTypeValueData[satId] = std::move(filteredTypeValueMap);
            }
        }
    }

    // Replace the original data with the filtered data
    obsData.satTypeValueData.swap(filteredSatTypeValueData);
}

// L1C => L1
// L2W => L2
void convertObsType(ObsData &obsData) {
    SatTypeValueMap stvData;
    for (auto sd: obsData.satTypeValueData) {
        TypeValueMap tvData;
        for (auto td: sd.second) {
            tvData[td.first.substr(0, 2)] = td.second;
        }
        stvData[sd.first] = tvData;
    }

    // 替代
    obsData.satTypeValueData = stvData;
};

std::map<SatID, Xvt> computeSatPos(ObsData &obsData,
                                   RinexNavStore& navStore,
                                   std::map<SatID, double>* prMap)
{
    std::map<SatID, Xvt> satXvtData;
    SatIDSet satRejectedSet;
    CommonTime time = obsData.epoch;

    // 遍历所有卫星观测
    for (auto stv: obsData.satTypeValueData)
    {
        SatID sat(stv.first);
        Xvt xvt;
        double obsCorr = 0.0;
        string codeType;
        double tgd = 0.0;

        // 标记：是否使用外部传入的伪距(IF组合)
        bool useExtPR = (prMap != nullptr) && (prMap->find(sat) != prMap->end());

        // 区分系统，读取TGD、配置单频码类型
        if (sat.system == "G")
        {
            codeType = "C1";
            NavEphGPS eph = navStore.findGPSEph(sat, time);
            tgd = eph.TGD;
        }
        else if (sat.system == "C")
        {
            codeType = "C2";   // 北斗 B1I
            NavEphBDS eph = navStore.findBDSEph(sat, time);
            tgd = eph.TGD1;
        }
        else
        {
            satRejectedSet.insert(sat);
            continue;
        }

        try
        {
            if (useExtPR)
            {
                // 分支1：使用外部伪距（IF组合伪距，已提前做TGD修正）
                obsCorr = prMap->at(sat);
            }
            else
            {
                // 分支2：原有逻辑，使用本机单频非组合伪距 + TGD修正
                double obsRaw = stv.second.at(codeType);
                obsCorr = obsRaw - C_MPS * tgd;

                if(debug)
                {
                    cout << "SAT:" << sat
                         << " RAW:" << obsRaw
                         << " CORR:" << obsCorr
                         << " TGD:" << tgd << endl;
                }
            }
        }
        catch (...)
        {
            satRejectedSet.insert(sat);
            continue;
        }

        // 计算发射时刻 + 卫星状态
        try
        {
            xvt = computeAtTransmitTime(time, obsCorr, sat, navStore);

            if(debug)
            {
                const double C_MPS = 299792458.0;
                double elaptc = obsCorr / C_MPS;
                CommonTime transmitTime = time - elaptc;
                cout << "Transmit Time: " << CommonTime2CivilTime(transmitTime) << endl;
            }
        }
        catch (InvalidRequest &e)
        {
            satRejectedSet.insert(sat);
            continue;
        }

        satXvtData[sat] = xvt;
    }

    // 剔除异常卫星
    for (auto sat: satRejectedSet)
    {
        obsData.satTypeValueData.erase(sat);
    }

    return satXvtData;
}

// 定义 GPS/北斗频率
const double F1_GPS = 1575.42e6;
const double F2_GPS = 1227.60e6;
const double F1_BDS = 1561.098e6;
const double F2_BDS = 1207.140e6;

std::map<SatID, double> computeIFPseudorange(ObsData& obsData, RinexNavStore& navStore)
{
    std::map<SatID, double> ifPrMap;
    const double f1sq_gps = F1_GPS * F1_GPS;
    const double f2sq_gps = F2_GPS * F2_GPS;
    const double denom_gps = f1sq_gps - f2sq_gps;

    const double f1sq_bds = F1_BDS * F1_BDS;
    const double f2sq_bds = F2_BDS * F2_BDS;
    const double denom_bds = f1sq_bds - f2sq_bds;

    int total = 0, valid = 0;
    for (auto& stv : obsData.satTypeValueData)
    {
        total++;
        SatID sat = stv.first;
        auto& obs = stv.second;
        double P1 = 0.0, P2 = 0.0;
        double tgd = 0.0;
        bool validSat = false;

        for(auto& p : obs) cout << p.first << " ";
        cout << endl;

        if (sat.system == "G")
        {
            if (obs.find("C1") == obs.end() || obs.find("C2") == obs.end())
            {
                cout << "[SKIP] " << sat << " Missing GPS dual-frequency data\n";
                continue;
            }
            P1 = obs["C1"];
            P2 = obs["C2"];
            NavEphGPS eph = navStore.findGPSEph(sat, obsData.epoch);
            tgd = eph.TGD;
            validSat = true;
        }
        else if (sat.system == "C")
        {
            if (obs.find("C2") == obs.end() || obs.find("C7") == obs.end())
            {
                cout << "[SKIP] " << sat << " Missing BDS dual-frequency data\n";
                continue;
            }
            P1 = obs["C2"];
            P2 = obs["C7"];
            NavEphBDS eph = navStore.findBDSEph(sat, obsData.epoch);
            tgd = eph.TGD1;
            validSat = true;
        }

        if (!validSat) continue;

        double P_if = 0.0;
        if (sat.system == "G")
            P_if = (f1sq_gps * P1 - f2sq_gps * P2) / denom_gps;
        else if (sat.system == "C")
            P_if = (f1sq_bds * P1 - f2sq_bds * P2) / denom_bds;


        ifPrMap[sat] = P_if;
        valid++;
    }

    cout << "\n==== Summary ====\nTotal satellites: " << total
         << "\nValid IF satellites: " << valid << endl;
    return ifPrMap;
}

Xvt computeAtTransmitTime(const CommonTime &tr,
                          const double &pr,
                          const SatID &sat,
                          RinexNavStore& navStore)
noexcept(false)
{
    Xvt xvt;
    // 1. 粗算：卫星钟面发射时刻 (t_r - 飞行时间)
    CommonTime t_s_clock = tr - pr / C_MPS;
    // 2. 迭代初始值：卫星系统时刻
    CommonTime t_s_sys = t_s_clock;

    // 迭代2次足够收敛
    for (int i = 0; i < 2; ++i)
    {
        // 用【卫星系统时刻】查星历、钟差、相对论改正
        xvt = navStore.getXvt(sat, t_s_sys);
        // 关键修正：系统时刻 = 钟面时 + 卫星钟差 + 相对论改正
        t_s_sys = t_s_clock + xvt.clkbias + xvt.relcorr;
    }

    // 存储：最终收敛的【卫星系统发射时刻】
    xvt.transmitTime = t_s_sys;
    return xvt;
}

std::map<SatID, Xvt> earthRotation(
    Eigen::Vector3d &xyz,
    std::map<SatID, Xvt> &satXvtTransTime,
    CommonTime rxTime
) // 增加入参：接收时刻
{
    std::map<SatID, Xvt> satXvtRecTime;
    for(auto& stv: satXvtTransTime)
    {
        SatID sat = stv.first;
        Xvt xvt = stv.second;

        // ========== 核心修改：用现有运算符求时间差(总秒数) ==========
        CommonTime txTime = xvt.transmitTime;
        double dt = rxTime - txTime;   // 直接相减，得到传播时间(秒)

        double wt = OMEGA_EARTH * dt;

        Eigen::AngleAxisd rotZ(wt, Eigen::Vector3d::UnitZ());
        Eigen::Matrix3d Rz = rotZ.toRotationMatrix();

        Eigen::Vector3d posSat(xvt.x[0], xvt.x[1], xvt.x[2]);
        Eigen::Vector3d velSat(xvt.v[0], xvt.v[1], xvt.v[2]);

        Eigen::Vector3d posRot = Rz * posSat;
        Eigen::Vector3d velRot = Rz * velSat;

        Xvt xvtRecTime = xvt;
        xvtRecTime.x[0] = posRot.x();
        xvtRecTime.x[1] = posRot.y();
        xvtRecTime.x[2] = posRot.z();

        xvtRecTime.v[0] = velRot.x();
        xvtRecTime.v[1] = velRot.y();
        xvtRecTime.v[2] = velRot.z();

        satXvtRecTime[sat] = xvtRecTime;
    }
    return satXvtRecTime;
}

void computeElevAzim(Eigen::Vector3d& xyz,
                     std::map<SatID,Xvt> & satXvt,
                     SatValueMap& tempElevData,
                     SatValueMap& tempAzimData
)
{

    for(auto sx: satXvt)
    {
        SatID sat = sx.first;

        XYZ satXYZ = sx.second.x;

        // elevation
        double elev(0.0);
        double azim(0.0);
        elev = elevation(xyz, satXYZ);
        azim = azimuth(xyz, satXYZ);

        tempElevData[sat] = elev;
        tempAzimData[sat] = azim;
    }
};

double wavelengthOfMW(string sys, string L1Type, string L2Type) {
    double f1 = getFreq(sys, L1Type);
    double f2 = getFreq(sys, L2Type);
    double wavelength = C_MPS / (f1 - f2);
    return wavelength;
};

double varOfMW(string, string L1Type, string L2Type) {
    double var = sqrt(2.0) / 2 * 0.3;
    return var;
};

void detectCSMW(ObsData &obsData,
                std::map<Variable, int> &csFlagData,
                SatEpochValueMap &satEpochMWData,
                SatEpochValueMap &satEpochMeanMWData,
                SatEpochValueMap &satEpochCSFlagData)
{
    //==================
    // 初始化常数和static变量
    //==================
    double deltaTMax(120.0);
    double minCycles(2.0);

    // A structure used to store filter data for a SV.
    struct MWData {
        // Default constructor initializing the data in the structure
        MWData()
                : formerEpoch(BEGINNING_OF_TIME), windowSize(0), meanMW(0.0), varMW(0.0) {};

        CommonTime formerEpoch; ///< The previous epoch time stamp.
        int windowSize;         ///< Size of current window, in samples.
        double meanMW;          ///< Accumulated mean value of combination.
        double varMW;           ///< Accumulated std value of combination.
    };

    // 这个数据在下次调用时需要用到，所以定位为static变量
    static std::map<SatID, MWData> satMWData;

    //==========================
    // 逐个卫星做周跳探测
    //==========================
    // Loop through all the satellites
    CommonTime currentEpoch = obsData.epoch;
    SatIDSet badSatSet;
    for (auto stv: obsData.satTypeValueData) {
        SatID sat = (stv).first;
        string L1Type, L2Type, C1Type, C2Type;
        if (sat.system == "G") {
            L1Type = "L1";
            L2Type = "L2";
            C1Type = "C1";
            C2Type = "C2";
        }
        else if (sat.system == "C") {
            L1Type = "L2";   // BDS B1I
            L2Type = "L7";   // BDS B2I
            C1Type = "C2";   // BDS B1I 伪距
            C2Type = "C7";   // BDS B2I 伪距
        }

        {
            badSatSet.insert(sat);
        }

        // wavelengthMW of MW-combination, see LinearCombination
        double wavelengthMW = wavelengthOfMW(sat.system, L1Type, L2Type);
        double varianceMW = varOfMW(sat.system, L1Type, L2Type);

        double f1 = getFreq(sat.system, L1Type);
        double f2 = getFreq(sat.system, L2Type);

        if (debug) {
            cout << "f1:" << f1 << "f2:" << f2 << endl;
        }

        double L1Value, L2Value, C1Value, C2Value, mwValue;

        try {
            L1Value = stv.second.at(L1Type);
            L2Value = stv.second.at(L2Type);
            C1Value = stv.second.at(C1Type);
            C2Value = stv.second.at(C2Type);

            mwValue
                    = (f1 * L1Value - f2 * L2Value) / (f1 - f2)
                      - (f1 * C1Value + f2 * C2Value) / (f1 + f2);
        } catch (std::out_of_range) {
            // 无法构成mw，这个卫星观测值周跳无法探测，删除这个卫星
            badSatSet.insert(sat);
            continue; // 继续处理下一个卫星
        }

        satEpochMWData[sat][currentEpoch] = mwValue;

        if (debugCSMW) {
            cout << "L1Value:" << L1Value << endl;
            cout << "L2Value:" << L2Value << endl;
            cout << "C1Value:" << C1Value << endl;
            cout << "C2Value:" << C2Value << endl;
            cout << "mwValue:" << mwValue << endl;
            cout << "wavelength:" << C_MPS / (f1 - f2) << endl;
        }

        //-------------------
        double currentDeltaT(0.0);
        double currentBias(0.0);
        int csFlag(0.0);

        currentDeltaT = (currentEpoch - satMWData[sat].formerEpoch);
        satMWData[sat].formerEpoch = currentEpoch;
        if (debugCSMW) {
            cout << "currentDeltaT:" << currentDeltaT << endl;
        }
        // Difference between current value of MW and average value
        currentBias = std::abs(mwValue - satMWData[sat].meanMW);
        if (debugCSMW) {
            cout << "currentBias:" << currentBias << endl;
        }

        // Increment window size
        satMWData[sat].windowSize++;

        /**
         * cycle-slip condition
         * 1. if data interrupt for a given time gap, then cyce slip should be set
         * 2. if current bias is greater than 1 cycle and greater than 4 sigma of mean mw.
         */
        double sigLimit = 4 * std::sqrt(satMWData[sat].varMW);

        if (debugCSMW) {
            cout << "deltaTMax:" << deltaTMax << endl;
            cout << "wavelengthMW:" << wavelengthMW << endl;
            cout << "sigLimit:" << sigLimit << endl;
            cout << "minCycles:" << minCycles * wavelengthMW << endl;
        }

        // 波长有可能为负值
        if (currentDeltaT > deltaTMax ||
            currentBias > std::abs(minCycles * wavelengthMW) ||
            currentBias > sigLimit) {

            // reset the filter window size/meanMW/InitialVarofMW
            satMWData[sat].meanMW = mwValue;
            satMWData[sat].varMW = varianceMW;
            satMWData[sat].windowSize = 1;

            if (debugCSMW) {
                cout << "* CS happened!" << endl;
            }
            csFlag = 1.0;
        } else {
            // MW bias from the mean value
            double mwBias(mwValue - satMWData[sat].meanMW);
            double size(static_cast<double>(satMWData[sat].windowSize));

            // Compute average
            satMWData[sat].meanMW += mwBias / size;

            // Compute variance
            // Var(i) = Var(i-1) + [ ( mw(i) - meanMW)^2/(i)- 1*Var(i-1) ]/(i);
            satMWData[sat].varMW += (mwBias * mwBias - satMWData[sat].varMW) / size;
        }

        // for print
        satEpochMeanMWData[sat][currentEpoch] = satMWData[sat].meanMW;

        // 放大到mw数值，以方便绘图
        satEpochCSFlagData[sat][currentEpoch] = csFlag * mwValue;

        // 将周跳探测标志存到模糊度变量中
        Variable amb1(obsData.station, sat, static_cast<Parameter>(Parameter::ambiguity), ObsID(sat.system, L1Type));
        Variable amb2(obsData.station, sat, static_cast<Parameter>(Parameter::ambiguity), ObsID(sat.system, L2Type));

        csFlagData[amb1] = csFlag;
        csFlagData[amb2] = csFlag;
    }

    // 删除坏卫星
    for (auto sat: badSatSet)
        obsData.satTypeValueData.erase(sat);

};

void differenceStation(EquSys& equSysRover,
                       EquSys& equSysBase,
                       EquSys& equSysSD)
{
    // 逐个观测值类型取出类型
    std::map<EquID, EquData> obsEquDataDiff;
    VariableSet varSetDiff;
    for(auto& oe: equSysRover.obsEquData)
    {
        // 在参考站中查找当前观测值，如果没有找到就跳过;
        // 需要注意测站名是不同的， 只需要查找卫星号和观测值
        if(equSysBase.obsEquData.find(oe.first)==equSysBase.obsEquData.end())
        {
            continue;
        }

        // 取出参考站的线性化残差观测值
        EquData oeBase = equSysBase.obsEquData.at(oe.first);

        // 在参考站中查找类型的观测值，找到了就计算站间差分观测值
        double diffPrefit;
        diffPrefit = oe.second.prefit - oeBase.prefit;

        if(debug)
        {
            cout << ">>>>>>>>> differenceStation" << endl;
            cout << oe.first
            << "rover: " << oe.second.prefit
            << "base:  " << oeBase.prefit
            << "diffPrefit:" << diffPrefit
            << endl;
        }

        // 把当前卫星的tvDiff数据插入到gDataDiff;
        obsEquDataDiff[oe.first].prefit = diffPrefit;

        //------------------------------------------------------------
        // 因为对于短基线来说，可以不用估计电离层和对流层，
        // 这里为了简单起见，直接将电离层和对流层参数从站间差分未知参数表中删除
        // todo:
        // 更优雅的处理方式是在参数估计时，对电离层和电离层进行约束，
        // 并根据基线长度对约束的方差进行动态调整。
        // 比如：
        // iono = 0, sigmaIono = 0.001*0.001*baseline
        // trop = 0, sigmaTrop = 0.0001*.0001*baseline
        // 通过增加电离层、对流层约束方程，实现通用rtk定位模型
        //------------------------------------------------------------
        // 未知参数与流动站的参数是相同的。
        std::map<Variable, double> vcDataTemp;
        for(auto vc: oe.second.varCoeffData)
        {
            if(vc.first.getParaType() != Parameter::iono)
            {
                vcDataTemp[vc.first] = vc.second;
                varSetDiff.insert(vc.first);
            }
        }
        obsEquDataDiff[oe.first].varCoeffData = vcDataTemp;

        // 权函数
        double weightRover = oe.second.weight;
        double weightBase = oeBase.weight;
        double varDiff = 1.0/weightRover + 1.0/weightBase;
        obsEquDataDiff[oe.first].weight = 1.0/varDiff;
    }

    equSysSD.obsEquData = obsEquDataDiff;
    equSysSD.varSet = varSetDiff;

};



SatID findDatumSat(bool& firstEpoch,
                   SatValueMap& satElevData) {
    // 确定基准卫星，必须是上一个历元已经固定的卫星才能选作基准
    SatID datumSat;
    auto maxIt = max_element(satElevData.begin(),satElevData.end(),
                              [](const auto& a, const auto& b){ return a.second < b.second; } );
    datumSat = maxIt->first;
    return datumSat;
}

void differenceSat( SatID& datumSat,
                    EquSys& equSysSD,
                    EquSys& equSysDD ) {
    //----------------------------------------------------
    // 根据基准卫星，选择每个观测类型的观测值，并将其他的与基准卫星对应观测值求差
    // warning:
    // 因为星间单差需要消除接收机钟差和接收机端硬件延迟，
    // 因此必须为每个类型独立构建星间单差观测方程，而不能混合在一起；
    // 因此，基准观测值的方程数据，应该存在以观测类型为key键值的map中，
    // 由于观测类型我们采用了C1，C2，L1，L2作为名字，
    // 当采用GPS+BDS时，两个系统均存在L2，无法有效区分，
    // 因此，这里需要创建一个独立的数据结构ObsID来管理观测类型ID，
    // 其由两个成员构成，一个是obsType；一个是卫星系统system
    // 另一个简单的处理：
    // string obsStr = obsType + system;
    //----------------------------------------------------

    std::map<ObsID, EquData> datumEquData;
    std::map<EquID, EquData> otherEquData;

    std::map<EquID, EquData> equData;
    equData = equSysSD.obsEquData;
    if (debug)
    {
        cout << "differenceSat:" << endl;
        cout << "datumSat:" << datumSat << endl;
    }

    for(auto ed: equData)
    {
        if(ed.first.sat == datumSat)
        {
            ObsID obsID(ed.first.sat.system, ed.first.obsType);
            if(debug)
                cout << "datum obsid:" << obsID << endl;

            datumEquData[obsID] = ed.second;
        }
        else
        {
            otherEquData[ed.first] = ed.second;
        }
    }

    // dd
    // 先验残差求差；
    // dx，dy，dz的系数求差；
    // 接收机钟差进一步差分掉了；
    // 模糊度除了基准卫星，其他卫星变成双差模式，系数不变
    std::map<EquID, EquData> equDataDD;
    VariableSet varSetDD;
    for(auto ed: otherEquData)
    {
        // 先验残差
        // 需要在基准ObsID里找EquData，来构成星间差分，
        // 如果找不到就剔除这个卫星；
        // 因此需要捕获异常，来处理找不到的情况；
        cout << "differenceSat:" << "sat:" << ed.first.sat << endl;
        double prefitDatum;
        ObsID currentObsID = ObsID(ed.first.sat.system, ed.first.obsType);
        try {
            prefitDatum= datumEquData.at(currentObsID).prefit;

            // dd prefit
            double prefitDD = ed.second.prefit - prefitDatum;
            equDataDD[ed.first].prefit = prefitDD;

            // 系数与未知参数
            VariableDataMap vcDatum = datumEquData.at(currentObsID).varCoeffData;

            // 接收机钟差消除了，只保留了坐标和模糊度参数
            for(auto vc: ed.second.varCoeffData)
            {
                if( vc.first.getParaType()==Parameter::dX ||
                    vc.first.getParaType()==Parameter::dY ||
                    vc.first.getParaType()==Parameter::dZ )
                {
                    double coeffDiff;
                    coeffDiff = vc.second - vcDatum.at(vc.first);
                    equDataDD[ed.first].varCoeffData[vc.first] = coeffDiff;
                    varSetDD.insert(vc.first);
                }
                else if(vc.first.getParaType()==Parameter::ambiguity)
                {
                    equDataDD[ed.first].varCoeffData[vc.first] = vc.second;
                    varSetDD.insert(vc.first);
                }
            }

            // 双差的方差近似等于单差观测值的方差的和；
            double weightCurrent = ed.second.weight;
            double weightDatum = datumEquData.at(currentObsID).weight;
            double varDiff = 1.0/weightCurrent + 1.0/weightDatum;
            equDataDD[ed.first].weight = 1.0/varDiff;

            //
            // todo
            // 构建完整的方差协方差阵，并比较定位结果的不同
        }
        catch(...)
        {
            continue;
        }
    }
    equSysDD.obsEquData = equDataDD;
    equSysDD.varSet = varSetDD;
};

// 对于Kalman滤波来说，需要对流动站和参考站周跳进行周跳标识符的合并，
// 只要流动站和参考站对应频率模糊度有一个发生了周跳就需要对周跳进行合并；
// 可以通过重载函数来实现对现有函数功能的复用。
void differenceStation(EquSys& equSysRover, VariableDataMap& csFlagRover,
                       EquSys& equSysBase, VariableDataMap& csFlagBase,
                       EquSys& equSysSD, VariableDataMap& csFlagSD)
{
    differenceStation(equSysRover,
                      equSysBase,
                      equSysSD);

    string roverStation = equSysRover.station;

    // todo:
    // 去掉流动站和参考站的站名，否则无法查找并匹配周跳
    VariableDataMap tempFlagRover;
    for(auto vd: csFlagRover)
    {
        Variable tempVar = vd.first;
        tempVar.station = std::string("");
        tempFlagRover[tempVar] = vd.second;
    }

    // 去掉基准站名字
    VariableDataMap tempFlagBase;
    for(auto vd: csFlagBase)
    {
        Variable tempVar = vd.first;
        tempVar.station = std::string("");
        tempFlagBase[tempVar] = vd.second;
    }

    // 现在，从基准站中寻找流动站模糊度，如果找到了，就把周跳标志合并
    // 如果没找到，就跳过，说明无法形成站间差分观测
    for(auto vd: tempFlagRover) {
        double flagRover = vd.second;
        if (tempFlagBase.find(vd.first) != tempFlagBase.end())
        {
            double flagBase = tempFlagBase.at(vd.first);
            double flagSD(0.0);
            // 基准站或者流动站一个发生周跳，就标志周跳
            if(flagRover|| flagBase)
            {
                flagSD = 1.0;
            }
            Variable varSD = vd.first; // 得到流动站模糊度变量
            varSD.station = roverStation; // 把流动站名站再次赋值进来
            csFlagSD[varSD] = flagSD;
        }
    }

    // 单差周跳
    cout << "differenceStation:" << "csFlagRover:" << endl;
    for(auto cd:csFlagRover)
    {
        cout << "cs:" << cd.first << " flag:" << cd.second;
    }

    cout << "differenceStation:" << "csFlagBase:" << endl;
    for(auto cd:csFlagBase)
    {
        cout << "cs:" << cd.first << " flag:" << cd.second;
    }

    cout << "differenceStation:" << "csFlagSD:" << endl;
    for(auto cd:csFlagSD)
    {
        cout << "cs:" << cd.first << " flag:" << cd.second;
    }

};

void differenceSat( SatID& datumSat,
                    EquSys& equSysSD, VariableDataMap& csFlagSD,
                    EquSys& equSysDD, VariableDataMap& csFlagDD ) {
    //----------------------------------------------------
    // 根据基准卫星，选择每个观测类型的观测值，并将其他的与基准卫星对应观测值求差
    // warning:
    // 因为星间单差需要消除接收机钟差和接收机端硬件延迟，
    // 因此必须为每个类型独立构建星间单差观测方程，而不能混合在一起；
    // 因此，基准观测值的方程数据，应该存在以观测类型为key键值的map中，
    // 由于观测类型我们采用了C1，C2，L1，L2作为名字，
    // 当采用GPS+BDS时，两个系统均存在L2，无法有效区分，
    // 因此，这里需要创建一个独立的数据结构ObsID来管理观测类型ID，
    // 其由两个成员构成，一个是obsType；一个是卫星系统system
    // 另一个简单的处理：
    // string obsStr = obsType + system;
    //----------------------------------------------------

    std::map<ObsID, EquData> datumEquData;
    std::map<EquID, EquData> otherEquData;

    std::map<EquID, EquData> equData;
    equData = equSysSD.obsEquData;

    for(auto ed: equData)
    {
        if(ed.first.sat == datumSat)
        {
            ObsID obsID(ed.first.sat.system, ed.first.obsType);
            if(debug)
                cout << "datum obsid:" << obsID << endl;

            datumEquData[obsID] = ed.second;
        }
        else
        {
            otherEquData[ed.first] = ed.second;
        }
    }

    // dd
    // 先验残差求差；
    // dx，dy，dz的系数求差；
    // 接收机钟差进一步差分掉了；
    // 模糊度除了基准卫星，其他卫星变成双差模式，系数不变
    std::map<EquID, EquData> equDataDD;
    VariableSet varSetDD;
    for(auto ed: otherEquData)
    {
        // 先验残差
        // 需要在基准ObsID里找EquData，来构成星间差分，
        // 如果找不到就剔除这个卫星；
        // 因此需要捕获异常，来处理找不到的情况；
        cout << "differenceSat:" << "sat:" << ed.first.sat << endl;
        double prefitDatum;
        ObsID currentObsID = ObsID(ed.first.sat.system, ed.first.obsType);
        try {
            prefitDatum= datumEquData.at(currentObsID).prefit;

            // dd prefit
            double prefitDD = ed.second.prefit - prefitDatum;
            equDataDD[ed.first].prefit = prefitDD;

            // 系数与未知参数
            VariableDataMap vcDatum = datumEquData.at(currentObsID).varCoeffData;

            // 接收机钟差消除了，只保留了坐标和模糊度参数
            for(auto vc: ed.second.varCoeffData)
            {
                if( vc.first.getParaType()==Parameter::dX ||
                    vc.first.getParaType()==Parameter::dY ||
                    vc.first.getParaType()==Parameter::dZ )
                {
                    double coeffDiff;
                    coeffDiff = vc.second - vcDatum.at(vc.first);
                    equDataDD[ed.first].varCoeffData[vc.first] = coeffDiff;
                    varSetDD.insert(vc.first);
                }
                else if(vc.first.getParaType()==Parameter::ambiguity)
                {
                    std::pair<Variable, double> ambData;
                    for(auto vc2: vcDatum)
                    {
                        if(vc2.first.getParaType() == Parameter::ambiguity)
                        {
                            ambData.first = vc2.first;
                            ambData.second = vc2.second;
                        }
                    }
                    // 把基准模糊度插入到方程中，也就是估计基准模糊度，而不是合并成双差模糊度
                    // warning: 是负号
                    equDataDD[ed.first].varCoeffData[ambData.first] = -ambData.second;
                    equDataDD[ed.first].varCoeffData[vc.first] = vc.second;
                    varSetDD.insert(ambData.first);
                    varSetDD.insert(vc.first);
                }
            }

            // 双差的方差近似等于单差观测值的方差的和；
            double weightCurrent = ed.second.weight;
            double weightDatum = datumEquData.at(currentObsID).weight;
            double varDiff = 1.0/weightCurrent + 1.0/weightDatum;
            equDataDD[ed.first].weight = 1.0/varDiff;

            //
            // todo
            // 构建完整的方差协方差阵，并比较定位结果的不同
        }
        catch(...)
        {
            continue;
        }
    }
    equSysDD.obsEquData = equDataDD;
    equSysDD.varSet = varSetDD;

    // 直接把站间单差模糊度标志给双差即可，因为估计的模糊度仍然为站间单差模糊度
    csFlagDD = csFlagSD;

};

void fixSolution(VectorXd& stateVec,
                 MatrixXd& covMatrix,
                 VariableSet& varSet,
                 double& ratio,
                 Vector3d& dxyzFixed,
                 VariableDataMap& fixedAmbData)
{
    // 按照卫星把模糊度进行分类；
    VariableSet ambVarSet;
    for (auto var: varSet) {
        if(var.getParaType()==Parameter::ambiguity)
        {
            ambVarSet.insert(var);
        }
    };

    int numAmb = ambVarSet.size();
    int numXYZT = varSet.size() - numAmb;

    // 取出来星间差分模糊度ambVarSetSD的估值和方差，利用lambda方法固定
    VectorXd ambSol;
    MatrixXd ambCov;
    ambSol = stateVec.tail(numAmb);
    ambCov = covMatrix.block(numXYZT, numXYZT, numAmb, numAmb);

    if (debug) {
        cout << "ambVarSet" << endl;
        for (auto var: ambVarSet) {
            cout << var << " ";
        }
        cout << endl;

        cout << "ambSol" << endl;
        cout << ambSol.transpose() << endl;

        cout << "ambCov" << endl;
        cout << ambCov << endl;
    }

    // 如果ratio值大于3，则表明模糊度可以固定。
    ARLambda arLambda;
    VectorXd ambSolFixed = arLambda.resolve(ambSol, ambCov);

    int iamb=0;
    fixedAmbData.clear();
    for(auto var:ambVarSet)
    {
        fixedAmbData[var] = ambSolFixed(iamb);
        iamb++;
    }

    ratio = arLambda.squaredRatio;

    if (debug) {
        cout << "ratio:" << endl;
        cout << ratio << endl;
    }

    VectorXd xVecFixed;
    VectorXd xVec = VectorXd::Zero(numXYZT);

    // x/y/z
    xVec = stateVec.head(numXYZT);

    MatrixXd Qxx = covMatrix.block(0, 0, numXYZT, numXYZT);
    MatrixXd Qxb = covMatrix.block(0, numXYZT, numXYZT, numAmb);
    MatrixXd Qbb = covMatrix.block(numXYZT, numXYZT, numAmb, numAmb);

    if (debug) {
        cout << fixed << setprecision(5) << endl;

        cout << "covMatrix" << endl;
        cout << covMatrix << endl;

        cout << "Qxx" << endl;
        cout << Qxx << endl;

        cout << "Qxb" << endl;
        cout << Qxb << endl;

        cout << "Qbb" << endl;
        cout << Qbb << endl;
    }

    xVecFixed = xVec - Qxb * Qbb.inverse() * (ambSol - ambSolFixed);

    // return fixed solutions
    dxyzFixed = xVecFixed;

};

void ambiguityDatum(bool& firstEpoch,
                    SatID& datumSat,
                    VariableDataMap& fixedAmbData,
                    EquSys& equSysDD){

    // 对于第一个历元，直接将基准卫星模糊度固定为零即可。
    if(firstEpoch)
    {
        for(auto var:equSysDD.varSet)
        {
            if(var.getSat() == datumSat)
            {
                EquID equIDDatum;
                equIDDatum.sat = datumSat;
                equIDDatum.obsType = var.getObsID().toString();

                EquData equDataDatum;
                equDataDatum.prefit = 0.0;
                equDataDatum.varCoeffData[var] = 1.0;
                equDataDatum.weight = 1.0E+8;

                // 将模糊度基准观测方程加入到观测系统中
                equSysDD.obsEquData[equIDDatum] = equDataDatum;
            }
        }
    }
    else
    {
        for(auto vd: fixedAmbData)
        {
            // 生成模糊度基准的观测方程
            if(vd.first.getSat()==datumSat)
            {
                EquID equIDDatum;
                equIDDatum.sat = datumSat;
                equIDDatum.obsType = vd.first.getObsID().toString();

                EquData equDataDatum;
                equDataDatum.prefit = vd.second;
                equDataDatum.varCoeffData[vd.first] = 1.0;
                equDataDatum.weight = 1.0E+8;

                // 将模糊度基准观测方程加入到观测系统中
                equSysDD.obsEquData[equIDDatum] = equDataDatum;
            }
        }
    }
};

// print solution to files
void printSolution(std::fstream & solStream,
                   CommonTime& ctTime,
                   Eigen::Vector3d& xyzRover,
                   Eigen::Vector3d& xyzRTKFloat,
                   double& ratio,
                   Eigen::Vector3d& xyzRTKFixed)
{
    YDSTime ydsTime = CommonTime2YDSTime(ctTime);
    solStream
            << ydsTime
            << fixed << setprecision(3)
            << "spp: " << xyzRover.transpose()
            << " float-rtk: " << xyzRTKFloat.transpose()
            << " ratio:" << ratio
            << " fixed-rtk:"<< xyzRTKFixed.transpose()
            << endl;
};

// print solution to files
void printSolution(std::fstream & solStream,
                   CommonTime& ctTime,
                   Eigen::Vector3d& xyzRover,
                   Eigen::Vector3d& xyzRTKFloat)
{
    YDSTime ydsTime = CommonTime2YDSTime(ctTime);
    solStream
    << ydsTime
    << fixed << setprecision(3)
    << "spp: " << xyzRover.transpose()
    << " rtk: " << xyzRTKFloat.transpose() << endl;
};

void printSolution(std::fstream & solStream,
                   CommonTime& ctTime,
                   Eigen::Vector3d& xyzRover)
{
    YDSTime ydsTime = CommonTime2YDSTime(ctTime);
    solStream
    << ydsTime
    << " "
    << fixed << setprecision(3)
    << xyzRover.transpose() << endl;
};

// =============================================================================
// 电离层延迟改正 Klobuchar 模型
// 输入：测站坐标、时间、卫星高度角/方位角
// 输出：每颗卫星的电离层延迟 (m)
// 完全匹配你截图的接口设计
// =============================================================================
std::map<SatID, double> ionoDelay(
    Eigen::Vector3d& xyz,
    CommonTime& epoch,
    std::map<SatID, double>& satElevData,
    std::map<SatID, double>& satAzimData,
    RinexNavStore& navStore
)
{
    const double PI = 3.141592653589793;
    const double C_MPS = 299792458.0;

    // 1. 测站坐标 XYZ → 经纬度 (rad)
    double lat, lon, hgt;
    double x = xyz[0], y = xyz[1], z = xyz[2];
    double a = 6378137.0;
    double f = 1.0 / 298.257223563;
    double e2 = 2 * f - f * f;

    lon = atan2(y, x);
    double p = sqrt(x * x + y * y);
    lat = atan2(z, p * (1 - e2));
    double N;
    for (int i = 0; i < 5; i++) {
        N = a / sqrt(1 - e2 * sin(lat) * sin(lat));
        hgt = p / cos(lat) - N;
        lat = atan2(z, p * (1 - e2 * N / (N + hgt)));
    }

    std::map<SatID, double> ionoMap;

    // 3. 对每一颗卫星计算电离层延迟
    for (auto& entry : satElevData) {
        SatID sat = entry.first;
        double elevDeg = entry.second;
        double azimDeg = satAzimData[sat];

        string sys = sat.system;

        // 1. GPS/BDS 不同电离层高度
        double h_ion;
        if (sys == "G") h_ion = 350000.0;    // GPS 350km
        else if (sys == "C") h_ion = 375000.0; // BDS 375km
        else h_ion = 350000.0;

        // 2. GPS/BDS 不同电离层参数 α β
        double alpha[4] = {0}, beta[4] = {0};
        if (sys == "G") {
            alpha[0] = navStore.ionoCorrData["GPSA"][0];
            alpha[1] = navStore.ionoCorrData["GPSA"][1];
            alpha[2] = navStore.ionoCorrData["GPSA"][2];
            alpha[3] = navStore.ionoCorrData["GPSA"][3];
            beta[0]  = navStore.ionoCorrData["GPSB"][0];
            beta[1]  = navStore.ionoCorrData["GPSB"][1];
            beta[2]  = navStore.ionoCorrData["GPSB"][2];
            beta[3]  = navStore.ionoCorrData["GPSB"][3];
        }
        else if (sys == "C") {
            alpha[0] = navStore.ionoCorrData["BDSA"][0];
            alpha[1] = navStore.ionoCorrData["BDSA"][1];
            alpha[2] = navStore.ionoCorrData["BDSA"][2];
            alpha[3] = navStore.ionoCorrData["BDSA"][3];
            beta[0]  = navStore.ionoCorrData["BDSB"][0];
            beta[1]  = navStore.ionoCorrData["BDSB"][1];
            beta[2]  = navStore.ionoCorrData["BDSB"][2];
            beta[3]  = navStore.ionoCorrData["BDSB"][3];
        }

        double elev = elevDeg * PI / 180.0;
        double azim = azimDeg * PI / 180.0;
        YDSTime ydst = CommonTime2YDSTime(epoch);
        double tow = ydst.sod;

        // Klobuchar 公式
        double RE = 6378137.0;
        double psi = asin( RE * cos(elev) / (RE + h_ion) );
        psi = (PI/2.0) - elev - psi;

        double phiI = lat + psi * cos(azim);
        if (phiI >  0.415) phiI = 0.415;
        if (phiI < -0.415) phiI = -0.415;

        double lambdaI = lon + psi * sin(azim) / cos(phiI);
        double phiM = phiI + 0.064 * cos(lambdaI - 1.617);

        double t = lambdaI / PI * 43200.0 + tow;
        t = fmod(t, 86400.0);
        if (t < 0) t += 86400.0;

        double amp = alpha[0] + phiM * (alpha[1] + phiM * (alpha[2] + phiM * alpha[3]));
        double per = beta[0] + phiM * (beta[1] + phiM * (beta[2] + phiM * beta[3]));
        if (amp < 0) amp = 0;
        if (per < 72000) per = 72000;

        double xVal = 2 * PI * (t - 50400.0) / per;
        double iono = 5.0e-9;
        if (fabs(xVal) < 1.57)
            iono += amp * cos(xVal);

        double f = 1.0 / sqrt(1.0 - pow( (RE * cos(elev))/(RE + h_ion), 2 ));
        ionoMap[sat] = iono * f * C_MPS;
    }

    return ionoMap;
}

// =============================================================================
// 对流层延迟改正
// 模型：Saastamoinen 天顶延迟 + Niell 投影映射函数
// 规范工程实现，适配GNSS标准SPP定位
// =============================================================================
std::map<SatID, double> tropDelay(
    Eigen::Vector3d& xyz,
    std::map<SatID, double>& satElevData,
    std::map<SatID, double>& satAzimData,
    double sod,
    std::ofstream& out_file
)
{
    const double PI = 3.141592653589793;
    std::map<SatID, double> tropMap;

    // 1. XYZ -> BLH(rad / m)
    double lat, lon, hgt;
    double x = xyz[0], y = xyz[1], z = xyz[2];
    double a = 6378137.0;
    double f = 1.0 / 298.257223563;
    double e2 = 2 * f - f * f;

    lon = atan2(y, x);
    double p = sqrt(x * x + y * y);
    lat = atan2(z, p * (1 - e2));
    double N;
    for (int i = 0; i < 5; i++)
    {
        N = a / sqrt(1.0 - e2 * sin(lat) * sin(lat));
        hgt = p / cos(lat) - N;
        lat = atan2(z, p * (1.0 - e2 * N / (N + hgt)));
    }

    // 2. 标准气象参数（经验值）
    double lat_deg  = lat * 180.0 / PI;
    double hgt_km   = hgt / 1000.0;
    double P = 1013.25 * pow(1.0 - 2.26e-5 * hgt, 5.225);
    double T = 15.0 - 0.0065 * hgt + 273.15;
    double RH = 0.5;
    double e = 0.000061078 * RH * exp(17.27 * (T-273.15) / (T-35.85));

    // 3. Saastamoinen 天顶干/湿延迟
    double ZHD = 0.0022768 * P / (1.0 - 0.00266 * cos(2*lat) - 0.00028 * hgt_km);
    double ZWD = 0.002277 * (1255.0 / T + 0.05) * e;

    // 4. Niell 干项系数
    double ahd[3] = { 2.53e-3, 5.49e-3, 1.14e-3 };
    double bh[3]  = { 2.91e-3, 1.62e-3, 8.3e-4  };
    double ch[3]  = { 54.0,    53.0,   53.0    };

    int lat_idx;
    if     (lat_deg < 15.0) lat_idx = 0;
    else if(lat_deg < 30.0) lat_idx = 1;
    else                    lat_idx = 2;

    double ah_val = ahd[lat_idx];
    double bh_val= bh[lat_idx];
    double ch_val = ch[lat_idx];

    // 5. 逐卫星计算斜对流层延迟
    for(auto& entry : satElevData)
    {
        SatID sat = entry.first;
        double elevDeg = entry.second;
        double elev = elevDeg * PI / 180.0;
        double sinE = sin(elev);

        // Niell 干映射函数
        double mh = (1.0 + ah_val/(1.0+bh_val/(1.0+ch_val/sinE)))
                  / (sinE + ah_val/(sinE+bh_val/(sinE+ch_val)));

        // 简易湿映射（工程常用简化）
        double mw = 1.0 / (sinE + 0.00095);

        // 拆分斜干、斜湿分量
        double slant_zhd = ZHD * mh;
        double slant_zwd = ZWD * mw;
        // 总斜路径对流层延迟
        double trop = slant_zhd + slant_zwd;
        tropMap[sat] = trop;

        // 写入干湿分量txt
        out_file << fixed << setprecision(6)
                 << sod << " "
                 << sat.toString() << " "
                 << elevDeg << " "
                 << slant_zhd << " "
                 << slant_zwd << " "
                 << trop << endl;
    }

    return tropMap;
}

// 计算站星几何距离
double calcGeometricRange(const Eigen::Vector3d& rcvPos, const Eigen::Vector3d& satPos)
{
    return (rcvPos - satPos).norm();
}

// 反向验证主逻辑
void verifyTransmitTime(
    const CommonTime& recvTime,
    const Eigen::Vector3d& rcvPos,
    const std::map<SatID, Xvt>& satXvtRec,
    std::ofstream& outFile
)
{
    static bool headerWritten = false;
    if (!headerWritten)
    {
        outFile << "RcvSOD(s),Sat,Delta_Tx(us)\n";
        headerWritten = true;
        outFile.flush();   // 表头强制刷盘
    }

    const double C_MPS = 299792458.0;
    double rcvSod = CommonTime2YDSTime(recvTime).sod;

    for (const auto& item : satXvtRec)
    {
        SatID sat = item.first;
        const Xvt& xvt = item.second;

        Eigen::Vector3d satPos(xvt.x[0], xvt.x[1], xvt.x[2]);
        double range = (rcvPos - satPos).norm();
        double flightT = range / C_MPS;
        CommonTime txBwd = recvTime - flightT;

        double deltaSec = xvt.transmitTime - txBwd;
        double deltaUs = deltaSec * 1e6;

        outFile << std::fixed << std::setprecision(4)
                << rcvSod << ","
                << sat.toString() << ","
                << deltaUs << "\n";

        outFile.flush();  // 每条数据写完立刻刷入磁盘
    }
}

void verifyTransmitTimeCompare(
    const CommonTime& recvTime,
    const Eigen::Vector3d& rcvPos,
    const std::map<SatID, Xvt>& satSingle,
    const std::map<SatID, Xvt>& satIf,
    std::ofstream& outFile
)
{
    static bool headerWritten = false;
    if (!headerWritten)
    {
        outFile << "RcvSOD(s),Sat,Delta_Single(us),Delta_IF(us),Delta_IF-Single(us)\n";
        headerWritten = true;
        outFile.flush();
    }

    const double C_MPS = 299792458.0;
    double rcvSod = CommonTime2YDSTime(recvTime).sod;

    for (const auto& item : satSingle)
    {
        SatID sat = item.first;
        auto itIf = satIf.find(sat);
        if (itIf == satIf.end()) continue;

        const Xvt& xvtSingle = item.second;
        const Xvt& xvtIf = itIf->second;

        // 计算单频误差
        Eigen::Vector3d posS(xvtSingle.x[0], xvtSingle.x[1], xvtSingle.x[2]);
        double rangeS = (rcvPos - posS).norm();
        double tauS = rangeS / C_MPS;
        CommonTime txBwd_S = recvTime - tauS;
        double deltaS_us = (xvtSingle.transmitTime - txBwd_S) * 1e6;

        // 计算IF误差
        Eigen::Vector3d posIF(xvtIf.x[0], xvtIf.x[1], xvtIf.x[2]);
        double rangeIF = (rcvPos - posIF).norm();
        double tauIF = rangeIF / C_MPS;
        CommonTime txBwd_IF = recvTime - tauIF;
        double deltaIF_us = (xvtIf.transmitTime - txBwd_IF) * 1e6;

        double diff_us = deltaIF_us - deltaS_us;

        outFile << std::fixed << std::setprecision(4)
                << rcvSod << ","
                << sat.toString() << ","
                << deltaS_us << ","
                << deltaIF_us << ","
                << diff_us << "\n";
        outFile.flush();
    }
}