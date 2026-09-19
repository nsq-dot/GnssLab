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

#include <iomanip>
#include "SolverLSQ.h"
#include <fstream>

// 诊断默认关闭。这里打印整张 hMatrix / wMatrix / state，逐历元都来一遍；
// 只影响 stdout，不参与任何数值计算，所以开关不可能移动回归基线。
#ifndef GNSSLAB_DEBUG_SOLVER
#define GNSSLAB_DEBUG_SOLVER 0
#endif

// CoordConvert.h 已经给 debug 提供了默认值，必须先撤销再重定义。
#undef debug
#define debug GNSSLAB_DEBUG_SOLVER

using namespace std;


void SolverLSQ::solve(EquSys &equSys) {

    if(debug)
        cout << "SolverLSQ:" << endl;

    currentUnkSet = equSys.varSet;
    int numUnk = currentUnkSet.size();
    int numObs = equSys.obsEquData.size();

    VectorXd prefit = VectorXd::Zero(numObs);
    MatrixXd hMatrix = MatrixXd::Zero(numObs, numUnk);
    MatrixXd wMatrix = MatrixXd::Zero(numObs, numObs);

    int iobs(0);
    for (auto ed: equSys.obsEquData) {
        prefit(iobs) = ed.second.prefit;

        for (auto vc: ed.second.varCoeffData) {
            // 从整体的X中搜索当前未知参数的位置
            int indexUnk = getIndex(currentUnkSet, vc.first);
            // 把偏导数插入到对应的h矩阵中
            hMatrix(iobs, indexUnk) = vc.second;
        }
        wMatrix(iobs, iobs) = ed.second.weight;

        iobs++;
    }

    MatrixXd hT = hMatrix.transpose();

    if (prefit.size()!= hMatrix.rows()) {
        InvalidSolver e("prefit size don't equal with rows of hMatrix");
        throw(e);
    }

    if (debug) {

        cout << "prefit:" <<endl;
        cout << prefit <<endl;

        cout << "hMatrix:" <<endl;
        cout << hMatrix <<endl;

        cout << "wMatrix:" <<endl;
        cout << wMatrix <<endl;
    }

    try {
        covMatrix = hT * wMatrix * hMatrix;
        covMatrix = covMatrix.inverse();
    }
    catch (...) {
        InvalidSolver e("Unable to invert matrix covMatrix");
        throw (e);
    }

    state = covMatrix * hT * wMatrix * prefit;

    if(debug)
    {
        cout << "state" << endl;
        cout << state.transpose() << endl;
    }

    double dx = getSolution(Parameter::dX, currentUnkSet, state);
    double dy = getSolution(Parameter::dY, currentUnkSet, state);
    double dz = getSolution(Parameter::dZ, currentUnkSet, state);

    dxyz[0] = dx;
    dxyz[1] = dy;
    dxyz[2] = dz;

}

int SolverLSQ::getIndex(const VariableSet &varSet, const Variable &thisVar) {
    int index(0);
    for (auto var: varSet) {
        if (var == thisVar) {
            return index;
        }
        index++;
    }
    // Not found. This used to fall through and return varSet.size(), which the
    // caller in solve() then used as a COLUMN INDEX into hMatrix - one past the
    // last column, i.e. a silent out-of-bounds write into the next row (or off
    // the end of the buffer for the last row). Throwing turns a memory
    // corruption into a diagnosable error.
    InvalidRequest e("SolverLSQ::getIndex: variable not present in varSet.");
    throw (e);
};

double SolverLSQ::getSolution(Parameter::ParameterName paraType,
                              VariableSet &currentUnkSet,
                              const VectorXd &stateVec)
noexcept(false) {
    // The end-of-range test used to sit INSIDE the loop, after the dereference,
    // so a parameter that was absent - or an entirely empty unknown set, which
    // is what an epoch whose double differences all got dropped produces -
    // dereferenced end(). Checking at the top is the same for every input that
    // found the parameter, and defined for the ones that did not.
    int index(0);
    for (auto varIt = currentUnkSet.begin(); varIt != currentUnkSet.end(); ++varIt, ++index) {
        if ((*varIt).getParaType() == paraType) {
            return stateVec(index);
        }
    }
    InvalidRequest e("SolverLSQ::Type not found in state vector.");
    throw (e);

}  // End of method 'SolverGeneral::getSolution()'


