/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Author: Shoujian Zhang，shjzhang@sgg.whu.edu.cn， 2024-10-10
 *
 * References:
 * 1. Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
 *    GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
 * 2. Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
 */
#pragma once

#include <vector>
#include <set>
#include <map>
#include <iostream>
#include <iomanip> // 包含此头文件以使用 std::fixed 和 std::setprecision
#include <string>
#include <sstream>
#include <stdexcept>
#include <Eigen/Eigen>

#include "TimeConvert.h"
#include "CoordStruct.h"
#include "TimeStruct.h"

//---------------
// 卫星号管理
//---------------
struct SatID {
    string system;
    int id;

    //
    // todo:
    // 增加这个字段，实现北斗2代和北斗3代的区分，
    // 在后面星间差分观测值构建时，需要考虑北斗2和北斗3接收机钟差不同
    // 引起的模型差异
    // int generation;

    // 构造函数
    SatID() : system(""), id(-1) {}

    // 从字符串构造函数
    SatID(string satStr) {
        system = satStr.substr(0, 1);
        id = stoi(satStr.substr(1, 2));
    }

    // Overload the equality operator as a member function
    bool operator==(const SatID &other) const {
        return this->system == other.system && this->id == other.id;
    }

    // Overload the not equal operator as a member function
    bool operator!=(const SatID &other) const {
        return !(*this == other);
    }

    // Overload the less-than operator as a member function
    bool operator<(const SatID &other) const {
        if (this->system != other.system)
            return this->system < other.system;
        return this->id < other.id;
    }

    std::string toString() const {
        std::stringstream sstream;
        sstream << system
                << std::setw(2) << std::setfill('0') << id;
        return sstream.str();
    }
};

typedef std::set<SatID> SatIDSet;

// 为避免因多次包含头文件导致非inline函数多重定义错误，有两种解决方案：
// 1. 将函数声明为 inline 并在头文件中定义，使得每个包含该头文件的源文件都有函数体副本但不会链接冲突。
// 2. 在头文件中仅声明函数，在单独的源文件中定义函数，确保整个程序中只有一个函数定义，避免重复定义错误。
// 全局重载的 << 运算符
inline ostream &operator<<(ostream &os, const SatID &satid) {
    os << satid.system
       << std::setw(2) << std::setfill('0') << satid.id;
    return os;
}


//---------------
// 观测值管理
//---------------
//---------------
// struct used in rinex reader
//---------------
struct RinexHeader {
    RinexHeader() {};

    string station;
    double version;              //< RINEX 3 version/type
    XYZ antennaPosition;         //< APPROX POSITION XYZ
    std::map<string, std::vector<string>> mapObsTypes;        //< SYS / # / OBS TYPES
};


typedef std::map<string, double> TypeValueMap;

inline std::ostream &operator<<(std::ostream &os, const TypeValueMap &typeValueMap) {
    for (const auto &entry: typeValueMap) {
        os << "  Type: " << entry.first
           << ", Value: " << std::fixed << std::setprecision(6) << entry.second << "\n";
    }
    return os;
}

typedef std::map<SatID, double> SatValueMap;
inline std::ostream &operator<<(std::ostream &os, const SatValueMap &satValueMap) {
    for (const auto &entry: satValueMap) {
        os << "  sat: " << entry.first
           << ", Value: " << std::fixed << std::setprecision(6) << entry.second << "\n";
    }
    return os;
}


typedef std::map<SatID, TypeValueMap> SatTypeValueMap;

inline std::ostream &operator<<(std::ostream &os, const SatTypeValueMap &satTypeValueMap) {
    for (const auto &satEntry: satTypeValueMap) {
        os << "Satellite ID: " << satEntry.first << "\n";
        os << satEntry.second; // 这里使用了 TypeValueMap 的输出函数
    }
    return os;
}

struct ObsData {
    Eigen::Vector3d antennaPosition;
    string station;
    CommonTime epoch;
    SatTypeValueMap satTypeValueData;
    // 新增多普勒：<卫星ID, <频点, 多普勒D(Hz)>>
    std::map<SatID, std::map<std::string, double>> satDopplerData;
    double sod;
};

inline std::ostream &operator<<(std::ostream &os, const ObsData &data) {

    os << "Epoch: " << CommonTime2CivilTime(data.epoch) << "\n"; // 使用 CommonTime 的输出函数

    if (!data.satTypeValueData.empty()) {
        os << "Satellite Data:\n";
        os << data.satTypeValueData; // 使用 SatTypeValueMap 的输出函数
    } else {
        os << "No satellite data available.\n";
    }

    return os;
}

typedef std::map<SatID, std::map<CommonTime, double>> SatEpochValueMap;

//-------------------
// 星历相关数据结构
//-------------------

/// ECEF position, velocity, clock bias and drift
class Xvt {
public:
    /// Default constructor
    Xvt() : x(0., 0., 0.), v(0., 0., 0.),
            clkbias(0.), clkdrift(0.) {};

    /// Destructor.
    virtual ~Xvt() {}

    /// access the position, ECEF Cartesian in meters
    Eigen::Vector3d getPos() throw() { return x; }

    /// access the velocity in m/s
    Eigen::Vector3d getVel() throw() { return v; }

    /// access the clock bias, in second
    double getClockBias() throw() { return clkbias; }

    /// access the clock drift, in second/second
    double getClockDrift() throw() { return clkdrift; }

    /// access the relativity correction, in seconds
    double getRelativityCorr() throw() { return relcorr; }

    // member data

    Eigen::Vector3d x;   ///< Sat position ECEF Cartesian (X,Y,Z) meters
    Eigen::Vector3d v;   ///< satellite velocity in ECEF Cartesian, meters/second
    double clkbias;      ///< Sat clock correction in seconds
    double clkdrift;     ///< satellite clock drift in seconds/second
    double relcorr;

    std::map<string, double> typeTGDData;

    CommonTime transmitTime;

}; // end class Xvt

// Output operator for Xvt
inline std::ostream &operator<<(std::ostream &os, Xvt &xvt)
throw() {
    os << setprecision(10) << "x:" << xvt.x.transpose() << endl;
    os << "v:" << xvt.v.transpose() << endl;
    os << "clk bias:" << xvt.clkbias << endl;
    os << "clk drift:" << xvt.clkdrift << endl;
    os << "relcorr:" << xvt.relcorr << endl;
    for(auto tv: xvt.typeTGDData)
        os << tv.first << "tgd:" << tv.second << endl;
    return os;
}

//---------------
// 参数估计模块数据结构
//---------------
class Parameter {
public:
    enum ParameterName { // 显式指定底层类型为int
        Unknown = 0, dX, dY, dZ, cdt,cdtBDS,
        dVX, dVY, dVZ, cdtDot, cdtDotBDS, // 测速新增
        ifb, iono, ambiguity, count
    };

    Parameter() {}

    Parameter(ParameterName _name)
            : paraName(_name) {}

    ParameterName getParaName() const {
        return this->paraName;
    }

    std::string toString() const {
        // 直接返回对应的字符串，假设调用方保证传入的颜色有效
        return paraNameStrings[static_cast<int>(paraName)];
    }

    ParameterName toParameterName(const std::string &nameStr) {
        for (int i = static_cast<int>(ParameterName::Unknown); i < static_cast<int>(ParameterName::count); ++i) {
            if (paraNameStrings[i] == nameStr) return static_cast<ParameterName>(i);
        }
        return ParameterName::Unknown;
    }

    // Overload the equality operator as a member function
    bool operator==(const Parameter &other) const {
        return this->paraName == other.paraName;
    }

    // Overload the not-equal operator as a member function
    bool operator!=(const Parameter &other) const {
        return !(*this == other);
    }

    // Overload the less-than operator as a member function
    bool operator<(const Parameter &other) const {
        return this->paraName < other.paraName;
    }

private:
    ParameterName paraName;
    static const string paraNameStrings[];
};

// 全局的 operator<< 函数
inline std::ostream &operator<<(std::ostream &os, const Parameter &v) {
    os << v.toString();
    return os;
}


class ObsID {
public:
    std::string satSys;
    std::string obsType;

    // 默认构造函数
    ObsID() : satSys(""), obsType("") {}

    // 使用两个字符串参数的构造函数
    ObsID(const std::string &sys, const std::string &type)
            : satSys(sys), obsType(type) {}

    // 重载相等运算符
    bool operator==(const ObsID &other) const {
        return this->satSys == other.satSys && this->obsType == other.obsType;
    }

    // 重载小于运算符（用于排序）
    bool operator<(const ObsID &other) const {
        if (this->satSys != other.satSys) {
            return this->satSys < other.satSys;
        }
        return this->obsType < other.obsType;
    }

    std::string toString() const {
        std::stringstream ss;
        ss << satSys << obsType;
        return ss.str();
    }

};

// 定义全局重载的 << 运算符
inline std::ostream &operator<<(std::ostream &os, const ObsID &obsid) {
    os << obsid.satSys << " " << obsid.obsType;
    return os;
}

class Variable {
public:
    // 默认构造函数
    Variable() {};

    //  (dx, dy, dz, cdt)
    Variable(const std::string &_station,
             Parameter _paraName)
            : station(_station), paraName(_paraName) {};

    // isb, ion, ambiguity
    Variable(const std::string _station,
             const SatID _sat,
             Parameter _paraName,
             ObsID _obsid)
            : station(_station), sat(_sat), obsID(_obsid), paraName(_paraName) {}

    Variable &operator=(const Variable &right) {
        if (this != &right) {
            this->station = right.station;
            this->sat = right.sat;
            this->obsID = right.obsID;
            this->paraName = right.paraName;
        }
        return *this;
    }

    bool operator<(const Variable &right) const;
    bool operator==(const Variable &right);
    bool operator!=(const Variable &right);

    // Getter方法

    std::string getStation() const { return station; }
    SatID getSat() const { return sat; } // 假设 SatID 是 string 类型
    ObsID getObsID() const { return obsID; }
    // 新增读取参数类型的接口
    Parameter::ParameterName getParaType() const {
        return paraName.getParaName();
    }
    // 添加 toString 方法
    std::string toString() const {
        std::stringstream ss;
        ss << "Variable{"
           << "station=" << station << ", "
           << "sat=" << sat << ", "
           << "obsID=" << obsID << ", "
           << "paraName=" << paraName
           << "}";
        return ss.str();
    }

    std::string station;
    SatID sat; // 如果 SatID 不是 string，请根据实际情况调整
    ObsID obsID;
    Parameter paraName;
};

// 全局的 operator<< 函数
inline std::ostream &operator<<(std::ostream &os, const Variable &v) {
    os << v.toString();
    return os;
}

typedef std::set<Variable> VariableSet;
typedef std::map<Variable, double> VariableDataMap;
typedef std::map<Variable, int> VariableIntMap;

//===========
// EquationData
//===========
class EquID {
public:
    SatID sat;          // 卫星标识（假设 SatID 已定义）
    std::string obsType; // 观测类型
//    std::string station; // 站点标识

    // 默认构造函数
    EquID() : sat(), obsType("") {}

    // 带参数的构造函数（假设 SatID 可从字符串构造）
    EquID(const SatID& sat, const std::string& type)
            : sat(sat), obsType(type) {}

    // 重载相等运算符
    bool operator==(const EquID& other) const {
        return this->sat == other.sat &&
               this->obsType == other.obsType ;
    }

    // 重载小于运算符（用于排序）
    bool operator<(const EquID& other) const {
        if (this->sat != other.sat) {
            return this->sat < other.sat;
        }
        else {
            return this->obsType < other.obsType;
        }
    }

    std::string toString() const {
        std::stringstream ss;
        ss << "obs{"
           << "sat=" << sat << ", "
           << "obsType=" << obsType
           << "}";
        return ss.str();
    }

};

// 全局的 operator<< 函数
inline std::ostream &operator<<(std::ostream &os, const EquID &equID) {
    os << equID.toString();
    return os;
}

struct EquData
{

    double prefit;
    std::map<Variable, double> varCoeffData;
    double weight;
};

// 所有观测方程数据，包括未知参数和每个方程的数据
struct EquSys
{
    // 每个观测方程的未知参数和系数及先验残差
    string station;
    std::map<EquID, EquData> obsEquData;
    // 整个方程系统的所有未知参数
    VariableSet varSet;
};

struct Result
{
    Eigen::Vector3d xyz, xyzFixed;
    Eigen::Vector3d blh, blhFixed;
    double sigDx, sigDy, sigDz;
    double pdop, gdop;
    int numSats;
    double ratio;
};