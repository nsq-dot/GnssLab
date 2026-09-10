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

#ifndef GNSSLAB_COORDSTRUCT_H
#define GNSSLAB_COORDSTRUCT_H

#include <iostream>
#include <cmath>
#include <stdexcept>
#include <memory>
#include <iomanip>
#include "Const.h"
#include <Eigen/Eigen>

using namespace std;

// 参考框架基类
class ReferenceFrame {
public:
    virtual ~ReferenceFrame() = default;

    // 获取长半轴
    virtual double getA() const = 0;

    // 获取扁率
    virtual double getF() const = 0;

    // 获取地球自转角速度
    virtual double getOmega() const = 0;

    // 获取地球引力常数
    virtual double getGM() const = 0;

    // 默认实现（可选）
    virtual double getJ2() const {
        throw std::runtime_error("getJ2() not implemented for this reference frame.");
    }

    // 计算第一偏心率平方 e^2
    double getE2() const {
        double f = getF();
        return 2 * f - f * f;
    }
};

// WGS84 参考框架
class WGS84 : public ReferenceFrame {
public:
    double getA() const override {
        return 6378137.0; // 长半轴 (米)
    }

    double getF() const override {
        return 1 / 298.257223563; // 扁率
    }

    double getOmega() const override {
        return 7.292115e-5; // 地球自转角速度 (弧度/秒)
    }

    double getGM() const override {
        return 3.986004418e14; // 地球引力常数 (米^3/秒^2)
    }
};

// GPS 广播星历采用的椭球参数与wgs84不同
class GPSEllipsoid : public WGS84
{
public:
    /// defined in ICD-GPS-200C, 20.3.3.4.3.3 and Table 20-IV
    /// @return angular velocity of Earth in radians/sec.
    virtual double angVelocity() const throw()
    { return 7.2921151467e-5; }

    /// defined in ICD-GPS-200C, 20.3.3.4.3.3 and Table 20-IV
    /// @return geocentric gravitational constant in m**3 / s**2
    virtual double gm() const throw()
    { return 3.986005e14; }

    /// derived from ICD-GPS-200C, 20.3.3.4.3.3 and Table 20-IV
    /// @return geocentric gravitational constant in km**3 / s**2
    virtual double gm_km() const throw()
    { return 3.9860034e5; }

    /// defined in ICD-GPS-200C, 20.3.4.3
    /// @return Speed of light in m/s.
    virtual double c() const throw()
    { return C_MPS; }

    /// derived from ICD-GPS-200C, 20.3.4.3
    /// @return Speed of light in km/s
    virtual double c_km() const throw()
    { return (C_MPS / 1000); }

}; // class GPSEllipsoid

// class CGCS2000, 请增加类，以管理bds的椭球
// CGCS2000 参考框架（北斗卫星导航系统使用）
class CGCS2000 : public ReferenceFrame {
public:
    double getA() const override {
        return 6378137.0;  // 长半轴 (m)
    }

    double getF() const override {
        return 1.0 / 298.257222101;  // 扁率
    }

    double getOmega() const override {
        return 7.2921150e-5;  // 地球自转角速度 (rad/s)
    }

    double getGM() const override {
        return 3.986004418e14;  // 地球引力常数 (m³/s²)
    }
};

// 北斗广播星历专用椭球（对应北斗 ICD 标准）
class BDSCGCS2000Ellipsoid : public CGCS2000
{
public:
    /// 北斗 ICD 规定的地球自转角速度 (rad/sec)
    virtual double angVelocity() const throw()
    { return 7.2921150e-5; }

    /// 北斗 ICD 规定的地球引力常数 (m³/s²)
    virtual double gm() const throw()
    { return 3.986004418e14; }

    /// 地球引力常数 (km³/s²)
    virtual double gm_km() const throw()
    { return 3.986004418e5; }

    /// 光速 (m/s)
    virtual double c() const throw()
    { return C_MPS; }

    /// 光速 (km/s)
    virtual double c_km() const throw()
    { return (C_MPS / 1000); }

};

// PZ90 参考框架
class PZ90 : public ReferenceFrame {
public:
    double getA() const override {
        return 6378136.0; // 长半轴 (米)
    }

    double getF() const override {
        return 1 / 298.257839303; // 扁率
    }

    double getOmega() const override {
        return 7.2921150e-5; // 地球自转角速度 (弧度/秒)
    }

    double getGM() const override {
        return 3.9860044e14; // 地球引力常数 (米^3/秒^2)
    }

    double getJ2() const override {
        return 1.08262575e-3; // 二阶田谐系数
    }
};

class XYZ : public Eigen::Vector3d {
public:
    // 默认构造函数
    XYZ() : Eigen::Vector3d(0.0, 0.0, 0.0)
    {};
    XYZ(const Eigen::Vector3d& vec) : Eigen::Vector3d(vec)
    {};
    // 带参数的构造函数
    XYZ(double x_, double y_, double z_) : Eigen::Vector3d(x_, y_, z_)
    {};
    double X() const
    { return this->x(); }
    double Y() const
    { return this->y(); }
    double Z() const
    { return this->z(); }

    // 成员函数形式重载减法运算符
    Eigen::Vector3d operator-(const XYZ& other) const {
        Eigen::Vector3d result;
        result[0] = this->x() - other.x();
        result[1] = this->y() - other.y();
        result[2] = this->z() - other.z();
        return result;
    }


};

class BLH : public Eigen::Vector3d {
public:
    BLH() : Eigen::Vector3d(0.0, 0.0, 0.0) {}
    // 默认构造函数
    BLH(const Eigen::Vector3d& vec) : Eigen::Vector3d(vec) {}
    // 带参数的构造函数
    BLH(double B_, double L_, double H_) : Eigen::Vector3d(B_, L_, H_) {}
    double B() const { return this->x(); }
    double L() const { return this->y(); }
    double H() const { return this->z(); }
};


#endif //GNSSLAB_COORDSTRUCT_H
