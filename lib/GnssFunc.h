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
