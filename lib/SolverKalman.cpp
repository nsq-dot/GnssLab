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

#include "SolverKalman.h"
#define debug 1

void SolverKalman::solve(EquSys& equSys, VariableDataMap &csData)
noexcept(false)
{

    //==================================================
    // 时间更新
    //==================================================
    currentUnkSet.clear();
    currentUnkSet = equSys.varSet;

    int numUnk = currentUnkSet.size();

    // 后面好几个地方都需要得到某一个变量在整体变量中的索引位置,为了提高
    // 计算效率,这里对当前某一个变量在全部变量中的位置进行索引,并存储起来;
    currentIndexData.clear();
    createIndex(currentUnkSet);

    // Feed the filter with the correct state and covariance matrix
    if( firstTime )
    {
        VectorXd initialState( numUnk );
        initialState.setZero();

        MatrixXd initialCov( numUnk, numUnk);
        initialCov.setZero();

        // Fill the initialCov matrix
        for( auto var: currentUnkSet )
        {
            int index = currentIndexData[var];
            initialCov(index,index) = 9.0E+10;
        }

        // Reset Kalman filter state and covariance matrix
        xhat = initialState;
        P    = initialCov;

        firstTime = false;
    }
    else
    {
        // Adapt the size to the current number of unknowns
        VectorXd currentState(numUnk);
        currentState.setZero();
        MatrixXd currentCov(numUnk, numUnk);
        currentCov.setZero();

        // Fill the state vector
        for( auto var: currentUnkSet )
        {
            int currIndex = currentIndexData[var];
            if( oldUnkSet.find(var)!=oldUnkSet.end())
            {
                int oldIndex = oldIndexData[var];
                currentState( currIndex ) = solution( oldIndex );
            }
        }

        // We need a copy of 'currentUnkSet'
        VariableSet tempSet(currentUnkSet);
        for( auto var: currentUnkSet )
        {
            int nowIndex = currentIndexData[var];
            if(oldUnkSet.find(var)!=oldUnkSet.end())
            {
                int oldIndex = oldIndexData[var];
                // 先拷贝方差
                currentCov(nowIndex, nowIndex) = covMatrix(oldIndex, oldIndex);

                // 删除对角元素, 计算剩余变量的协方差
                tempSet.erase(var);

                for( auto v2: tempSet )
                {
                    int nowIndex2 = currentIndexData[v2];

                    // if not found, the covaraince is set to default ZERO !!!
                    if( oldUnkSet.find(v2)!=oldUnkSet.end() )
                    {
                        int oldIndex2 = oldIndexData[v2];
                        currentCov(nowIndex, nowIndex2) = covMatrix(oldIndex, oldIndex2);
                        currentCov(nowIndex2, nowIndex) = covMatrix(oldIndex, oldIndex2);
                    }
                }
            }
            else
            {
                currentCov(nowIndex, nowIndex) = 9.0E+10;

                // 删除对角元素
                tempSet.erase(var);
            }

        }  // End of for( VariableSet::const_iterator itVar1 = currentUnkSet...'

        // Reset Kalman filter to current state and covariance matrix
        xhat = currentState;
        P    = currentCov;

    }  // End of 'if(firstTime)'


    if(debug)
    {
        cout << "phiMatrix" << endl;
    }

    MatrixXd phiMatrix(numUnk, numUnk);
    phiMatrix.setZero();

    MatrixXd qMatrix(numUnk, numUnk);
    qMatrix.setZero();

    // 根据变量的随机模型构建状态转移矩阵和噪声矩阵
    int ii = 0;
    for(auto var: currentUnkSet)
    {
        cout << "var:" << var << endl;

        // 接收机坐标
        if( var.getParaType() == Parameter::dX ||
            var.getParaType() == Parameter::dY ||
            var.getParaType() == Parameter::dZ
                )
        {
            phiMatrix(ii,ii) = 0.0;
            qMatrix(ii,ii) = 1.0E+4;
        }
        else if( var.getParaType() == Parameter::cdt ) // 单点定位由cdt
        {
            // 白噪声
            phiMatrix(ii,ii) = 0.0;
            qMatrix(ii,ii) = 9.0E+10;
        }
        else if(var.getParaType() == Parameter::ambiguity)
        {
            if(csData[var])
            {
                // 周跳,重置参数
                phiMatrix(ii,ii) = 0.0;
                qMatrix(ii,ii) = 9.0E+10; //??
            }
            else
            {
                // 常数模型
                phiMatrix(ii,ii) = 1.0;
                qMatrix(ii,ii) = 0.0; //??
            }
        }
        cout << "ii:" << ii << endl;
        cout << "qMatrix:" << qMatrix(ii,ii) << endl;
        ii++;
    }

    if(debug)
    {
        cout << "phiMatrix:" << endl;
        cout << phiMatrix << endl;
        cout << "qMatrix:" << endl;
        cout << qMatrix << endl;
    }

    //-----------------------------------------
    // Kalman滤波求解参数
    //-----------------------------------------

    if(debug)
    {
        cout << "xhat" << endl;
        cout << xhat << endl;
        cout << "P" << endl;
        cout << P << endl;
    }

    kalmanFilter.Reset(xhat, P);
    kalmanFilter.TimeUpdate(phiMatrix, qMatrix);

    cout << "xhatminus" << endl;
    cout << kalmanFilter.xhatminus << endl;

    cout << "Pminus" << endl;
    cout << kalmanFilter.Pminus << endl;

    //==================================================
    // 测量更新
    //==================================================
    int numObs = equSys.obsEquData.size();

    VectorXd prefit = VectorXd::Zero(numObs);
    MatrixXd hMatrix = MatrixXd::Zero(numObs, numUnk);
    MatrixXd wMatrix = MatrixXd::Zero(numObs, numObs);

    int iobs(0);
    for (auto ed: equSys.obsEquData) {

        if(debug)
            cout << "obs " << ed.first << "value:" << ed.second.prefit << endl;
        prefit(iobs) = ed.second.prefit;

        for (auto vc: ed.second.varCoeffData) {
            // 从整体的X中搜索当前未知参数的位置
            int indexUnk = currentIndexData.at(vc.first);
            // 把偏导数插入到对应的h矩阵中
            hMatrix(iobs, indexUnk) = vc.second;
            if(debug)
                cout << "var:" << vc.first << " coeff:" << vc.second << endl;
        }
        wMatrix(iobs, iobs) = ed.second.weight;

        if(debug)
        cout << "weight:" << ed.second.weight << endl;

        iobs++;
    }

    MatrixXd hT = hMatrix.transpose();

    if (prefit.size()!= hMatrix.rows()) {
        InvalidSolver e("prefit size don't equal with rows of hMatrix");
        throw(e);
    }

    kalmanFilter.MeasUpdate(prefit, hMatrix, wMatrix);

    if(debug)
    {
        cout << "after kalmanFilter MeasUpdate" << endl;
    }

    // 从kalman滤波中得到需要的参数
    solution = kalmanFilter.xhat;
    covMatrix = kalmanFilter.P;
    postfitResidual = kalmanFilter.postfitResidual;

    double dx = getSolution(Parameter::dX, currentUnkSet, solution);
    double dy = getSolution(Parameter::dY, currentUnkSet, solution);
    double dz = getSolution(Parameter::dZ, currentUnkSet, solution);

    dxyz[0] = dx;
    dxyz[1] = dy;
    dxyz[2] = dz;

    // 将矩阵结果存储到map中,以方便下一个历元的查找
    oldUnkSet = currentUnkSet;
    oldIndexData = currentIndexData;

    return;

}  // End of method 'SolverKalman::Process()'

void SolverKalman::createIndex(const VariableSet &varSet ){
    int index(0);
    for (auto var: varSet) {
        currentIndexData[var] = index;
        index++;
    }
};

double SolverKalman::getSolution(const Parameter &type,
                              VariableSet &currentUnkSet,
                              const VectorXd &stateVec)
noexcept(false) {
    // Declare an varIterator for 'stateMap' and go to the first element
    auto varIt = currentUnkSet.begin();
    int index(0);
    while ((*varIt).getParaType() != type) {
        // If the same type is not found, throw an exception
        if (varIt == currentUnkSet.end()) {
            InvalidRequest e("SolverLSQ::Type not found in state vector.");
            throw (e);
        }
        index++;
        varIt++;
    }

    // Else, return the corresponding value
    return stateVec(index);

}  // End of method 'SolverGeneral::getSolution()'