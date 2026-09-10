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

#ifndef ARLAMBDA_H_
#define ARLAMBDA_H_

#include <Eigen/Eigen>
#include "GnssFunc.h"

using namespace Eigen;

class ARLambda {
public:

    ARLambda()
            : squaredRatio(0) {};

    ~ARLambda() {};

    Eigen::VectorXd resolve(Eigen::VectorXd &ambFloat,
                            Eigen::MatrixXd &ambCov);

    bool isFixed(double threshhold = 3.0) {
        return (squaredRatio > threshhold) ? true : false;
    }

    double squaredRatio;

protected:

    // lambda/mlambda integer least-square estimation
    // a     Float parameters (n x 1)
    // Q     Covariance matrix of float parameters (n x n)
    // F     Fixed solutions (n x m)
    // s     Sum of squared residulas of fixed solutions (1 x m)
    // m     Number of fixed solutions
    //      status (0:ok,other:error)
    int lambda(Eigen::VectorXd &a,
               Eigen::MatrixXd &Q,
               Eigen::MatrixXd &F,
               Eigen::VectorXd &s,
               const int &m = 2);

    // Q = L'*diag(D)*L
    int factorize(Eigen::MatrixXd &Q,
                  Eigen::MatrixXd &L,
                  Eigen::VectorXd &D);

    void gauss(Eigen::MatrixXd &L,
               Eigen::MatrixXd &Z,
               int i,
               int j);

    void permute(Eigen::MatrixXd &L,
                 Eigen::VectorXd &D,
                 int j,
                 double del,
                 Eigen::MatrixXd &Z);

    void reduction(Eigen::MatrixXd &L,
                   Eigen::VectorXd &D,
                   Eigen::MatrixXd &Z);

    // Modified lambda (mlambda) search
    virtual int search(Eigen::MatrixXd &L,
                       Eigen::VectorXd &D,
                       Eigen::VectorXd &zs,
                       Eigen::MatrixXd &zn,
                       Eigen::VectorXd &s,
                       const int &m = 2);

};


#endif /* ARLAMBDA_H_ */
