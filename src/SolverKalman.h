//=====================================================
// Copyright:
//  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
//  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
//  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
//  To copy, use, and modify the software;
//  To use the software for commercial purposes;
//  To redistribute the software.
//
// References:
//  1.  Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
//      GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
//  2.  Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
//
// Author:
//  Shoujian Zhang，shjzhang@sgg.whu.edu.cn， 2024-10-10
//=====================================================

#ifndef GNSSLAB_SOLVERKALMAN_H
#define GNSSLAB_SOLVERKALMAN_H

#include <Eigen/Eigen>
#include "GnssStruct.h"
#include "KalmanFilter.hpp"

using namespace Eigen;

//>>>>>>>>>>>>>>>>>
// 这个类的目的是把把方程系统和随机模型转变为矩阵形式，然后调用矩阵运算的KalmanFilter类实现卡尔曼滤波计算；
// 通过定义通用的方程系统，这个类可以用于单点定位，精密单点定位和rtk定位或者卫星定轨等任务
// todo:
// 考虑到kalman滤波与最小二乘很多模块是相同的，比如参数排序，获取参数解等等，因此可以设计为继承类
// 这样可以进一步减少代码的重复
//>>>>>>>>>>>>>>>>>>>
class SolverKalman {

public:
    SolverKalman() : firstTime(true) {};

    virtual void solve(EquSys &equSys, VariableDataMap& csData);
    void createIndex(const VariableSet &varSet );

    int getIndex(const VariableSet &varSet, const Variable &thisVar);
    double getSolution(const Parameter &type,
                       VariableSet &currentUnkSet,
                       const VectorXd &stateVec);

    VectorXd getState()
    {
        return solution;
    };

    MatrixXd getCovMatrix()
    {
        return covMatrix;
    };

    Eigen::Vector3d getdxyz() const{
        return dxyz;
    };

    /// Destructor.
    virtual ~SolverKalman() {};

private:

    bool firstTime;

    VectorXd solution, xhat;
    MatrixXd covMatrix, P;
    VectorXd postfitResidual;
    Vector3d dxyz;

    VariableSet currentUnkSet;
    VariableSet oldUnkSet;
    VariableIntMap currentIndexData;
    VariableIntMap oldIndexData;

    KalmanFilter kalmanFilter;

};


#endif //GNSSLAB_SOLVERKALMAN_H
