#ifndef GNSSLAB_GNSSFUNC_H
#define GNSSLAB_GNSSFUNC_H

#include <string>
#include <set>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>
#include <map>
#include "CoordStruct.h"
#include "GnssStruct.h"
#include "StringUtils.h"
#include "RinexNavStore.hpp"

using namespace Eigen;

// Changing Sign
inline double sign(double x)
{
    return (x <= 0.0) ? -1.0 : 1.0;
};

// Rounding Values
inline double round(double x)
{
    return double(std::floor(x + 0.5));
};

// Swapping values
inline void swap(double& a, double& b)
{
    double t(a); a = b; b = t;
};

void parseRinexHeader(std::fstream &rinexFileStream,
                      RinexHeader &rinexHeader);

ObsData parseRinexObs(std::fstream &rinexFileStream);

CommonTime parseTime(const string &line);

void chooseObs(ObsData &obsData,
               std::map<string, std::set<string>> &sysTypes);

void convertObsType(ObsData &obsData);

double wavelengthOfMW(string sys,
                      string L1Type,
                      string L2Type);

double varOfMW(string,
               string L1Type,
               string L2Type);

//================
// 系统误差相关函数
//================
// GnssFunc.h 声明
std::map<SatID, Xvt> computeSatPos(ObsData &obsData,
                                   RinexNavStore& navStore,
                                   std::map<SatID, double>* prMap = nullptr);

Xvt computeAtTransmitTime(const CommonTime& tr,
                          const double& pr,
                          const SatID& sat,
                          RinexNavStore& navStore);

void computeElevAzim(Eigen::Vector3d& xyz,
                     std::map<SatID,Xvt> & satXvtTransTime,
                     SatValueMap& tempElevData,
                     SatValueMap& tempAzimData);


std::map<SatID, Xvt> earthRotation(
    Eigen::Vector3d &xyz,
    std::map<SatID, Xvt> &satXvtTransTime,
    CommonTime rxTime
);

// 正确的声明
std::map<SatID, double> computeIFPseudorange(ObsData& obsData, RinexNavStore& navStore);

// 反向验证：由卫星位置+接收机位置计算几何距离
double calcGeometricRange(const Eigen::Vector3d& rcvPos, const Eigen::Vector3d& satPos);

// 反向校验发射时刻：输出反向计算的发射时刻、与正向结果的差值
void verifyTransmitTime(
    const CommonTime& recvTime,
    const Eigen::Vector3d& rcvPos,
    const std::map<SatID, Xvt>& satXvtRec,
    std::ofstream& outFile
);

// 新增：双路对比反向验证（IF - 单频 差分）
void verifyTransmitTimeCompare(
    const CommonTime& recvTime,
    const Eigen::Vector3d& rcvPos,
    const std::map<SatID, Xvt>& satSingle,
    const std::map<SatID, Xvt>& satIf,
    std::ofstream& outFile
);

// todo
// ===========
// computeIonoDelay();
std::map<SatID, double> ionoDelay(
    Eigen::Vector3d& xyz,
    CommonTime& epoch,
    std::map<SatID, double>& satElevData,
    std::map<SatID, double>& satAzimData,
    RinexNavStore& navStore
);
// computeTropDelay();
std::map<SatID, double> tropDelay(
    Eigen::Vector3d& xyz,
    std::map<SatID, double>& satElevData,
    std::map<SatID, double>& satAzimData,
    double sod,                 // 新增：当日秒
    std::ofstream& out_file     // 新增：干湿分量输出文件流
);
//================

void detectCSMW(ObsData &obsData,
                std::map<Variable, int> &csFlagData,
                SatEpochValueMap &satEpochMWData,
                SatEpochValueMap &satEpochMeanMWData,
                SatEpochValueMap &satEpochCSFlagData);

//================
// 周跳探测：载波相位几何无关(GF)组合
//================
// L_I = L1 - L2，消掉几何距离/轨道/卫星钟/对流层，剩电离层、硬件延迟、
// 相位缠绕和模糊度。历元间一次差分后电离层项若可忽略，则
//     ΔL_I = λ1·ΔN1 - λ2·ΔN2
// 最小可探测值为 |λ1-λ2|（GPS 5.39 cm、BDS 5.63 cm）。
//
// 周跳标志的状态编码。MW 程序把"弧段首历元""数据中断""真周跳"一律记成 1，
// 于是每颗卫星的首历元都必然是一次假阳性，无法统计检出率与虚警率。
// GF 把三者分开编码，只有 CSGF_SLIP 代表"检出周跳"。
enum CSGFStatus {
    CSGF_OK = 0,     ///< 已判定，无周跳
    CSGF_SLIP = 1,   ///< 已判定，该历元发生周跳
    CSGF_INIT = 2,   ///< 弧段首历元，不做判定
    CSGF_GAP = 3,    ///< 距上一历元超过 deltaTMax，不做判定
    CSGF_WARMUP = 4  ///< 多项式窗口样本不足，不做判定
};

// 状态编码对应的名字，用于输出文件的状态列
const char *csGFStatusName(int status);

// GF 组合的自然尺度 |λ1-λ2| [m]，以及该组合的初始方差 [m^2]。
// 注意 threshold 与 wavelengthOfGF 无关：阈值是绝对米值，不是"周数 × 波长"。
double wavelengthOfGF(string sys, string L1Type, string L2Type);

double varOfGF(string sys, string L1Type, string L2Type);

// 习题1：基于历元间一次差分的 GF 组合周跳探测。
//
// threshold  —— 判定门限 [m]，默认 0.030（= 0.56·|λ1-λ2|）。取值必须低于
//               待检的最小周跳，直接设在 |λ1-λ2| 上对恰好这么多周的周跳
//               只有约一半的检出率。
// deltaTMax  —— 超过该间隔视为数据中断，只重置不做判定 [s]。
void detectCSGFdiff(ObsData &obsData,
                    std::map<Variable, int> &csFlagData,
                    SatEpochValueMap &satEpochGFData,
                    SatEpochValueMap &satEpochDLData,
                    SatEpochValueMap &satEpochMeanDLData,
                    SatEpochValueMap &satEpochSigmaDLData,
                    SatEpochValueMap &satEpochCSFlagData,
                    double threshold = 0.030,
                    double deltaTMax = 120.0);

// 习题3：基于二阶多项式拟合的 GF 组合周跳探测。
//
// 对重心化序列 S = L_I - L_I(anchor) 在滑动窗口上做二阶拟合，
// 窗口严格排除当前历元（样本外预报），用残差判定。
//
// windowLength —— 滑动窗口长度 [历元]，默认 30（1 Hz）/ 20（30 s）。
// threshold    —— 门限下限 [m]，默认 0.030。
// deltaTMax    —— 超过该间隔视为数据中断 [s]。
void detectCSGFpoly(ObsData &obsData,
                    std::map<Variable, int> &csFlagData,
                    SatEpochValueMap &satEpochGFData,
                    SatEpochValueMap &satEpochPolyPredData,
                    SatEpochValueMap &satEpochPolyResData,
                    SatEpochValueMap &satEpochSigmaResData,
                    SatEpochValueMap &satEpochCSFlagData,
                    int windowLength = 30,
                    double threshold = 0.030,
                    double deltaTMax = 120.0);

void differenceStation(EquSys& equSysRover, VariableDataMap& csFlagRover,
                       EquSys& equSysBase, VariableDataMap& csFlagBase,
                       EquSys& equSysSD, VariableDataMap& csFlagSD);

void differenceStation(EquSys& equSysRover,
                       EquSys& equSysBase,
                       EquSys& equSysSD);

SatID findDatumSat(bool& firstEpoch,
                   SatValueMap& satElevData);

void differenceSat( SatID& datumSat,
                    EquSys& equSysSD, VariableDataMap& csFlagSD,
                    EquSys& equSysDD, VariableDataMap& csFlagDD);

void differenceSat( SatID& datumSat,
                    EquSys& equSysSD,
                    EquSys& equSysDD);

void ambiguityDatum(bool& firstEpoch,
                    SatID& datumSat,
                    VariableDataMap& fixedAmbData,
                    EquSys& equSysDD);

void fixSolution(VectorXd& stateVec,
                 MatrixXd& covMatrix,
                 VariableSet& varSet,
                 double& ratio,
                 Vector3d& dxyzFixed,
                 VariableDataMap& fixedAmbData);

// print solution to files
void printSolution(std::fstream & solStream,
                   CommonTime& ctTime,
                   Eigen::Vector3d& xyzRover,
                   Eigen::Vector3d& xyzRTKFloat,
                   double& ratio,
                   Eigen::Vector3d& xyzRTKFixed);

// print solution to files
void printSolution(std::fstream & solStream,
                   CommonTime& ctTime,
                   Eigen::Vector3d& xyzRover,
                   Eigen::Vector3d& xyzRTKFloat);

// print solution to files
void printSolution(std::fstream & solStream,
                   CommonTime& ctTime,
                   Eigen::Vector3d& xyzRover);

#endif //GNSSLAB_GNSSFUNC_H
