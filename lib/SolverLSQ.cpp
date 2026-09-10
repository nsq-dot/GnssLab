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

#define debug 1
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
            break;
        }
        index++;
    }
    return index;
};

double SolverLSQ::getSolution(Parameter::ParameterName paraType,
                              VariableSet &currentUnkSet,
                              const VectorXd &stateVec)
noexcept(false) {
    auto varIt = currentUnkSet.begin();
    int index(0);
    while ((*varIt).getParaType() != paraType) {
        if (varIt == currentUnkSet.end()) {
            InvalidRequest e("SolverLSQ::Type not found in state vector.");
            throw (e);
        }
        index++;
        varIt++;
    }
    return stateVec(index);

}  // End of method 'SolverGeneral::getSolution()'   


