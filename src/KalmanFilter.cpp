#pragma ident "$Id$"

/**
 * @file KalmanFilter.cpp
 * Class to compute the solution using a Kalman filter.
 */

#include "Exception.h"
#include "KalmanFilter.hpp"

#define debug 0

using namespace std;


/* Reset method.
 *
 * This method will reset the filter, setting new values for initial
 * system state vector and the a posteriori error covariance matrix.
 *
 * @param initialState      Vector setting the initial state of
 *                          the system.
 * @param initialErrorCovariance    Matrix setting the initial
 *                   values of the a posteriori error covariance.
 */
void KalmanFilter::Reset(const VectorXd &initialState,
                         const MatrixXd &initialErrorCovariance) {

    xhat = initialState;
    P = initialErrorCovariance;

    xhatminus = VectorXd::Zero(initialState.size());
    Pminus = MatrixXd::Zero(initialErrorCovariance.rows(), initialErrorCovariance.cols());

}  // End of method 'KalmanFilter::Reset()'


// Compute the a posteriori estimate of the system state, as well as
// the a posteriori estimate error covariance matrix. This version
// assumes that no control inputs act on the system.
//
// @param phiMatrix         State transition matrix.
// @param qMatrix    Process noise covariance matrix.
// @param mVector      Measurements vector.
// @param hMatrix    Measurements matrix. Called geometry
//                              matrix in GNSS.
// @param wMatrix   Measurements noise covariance
//                                      matrix.
//
// @return
//  0 if OK
//  -1 if problems arose
//
int KalmanFilter::Compute(const MatrixXd &phiMatrix,
                          const MatrixXd &qMatrix,
                          const VectorXd &mVector,
                          const MatrixXd &hMatrix,
                          const MatrixXd &wMatrix)
noexcept(false) {

    try {
        Predict(phiMatrix,
                xhat,
                qMatrix);

        Correct(mVector,
                hMatrix,
                wMatrix);
    }
    catch (InvalidSolver e) {
        throw (e);
        return -1;
    }

    return 0;

}  // End of method 'KalmanFilter::Compute()'


/* Predicts (or "time updates") the a priori estimate of the
 * system state, as well as the a priori estimate error
 * covariance matrix.
 * This version assumes that no control inputs act on the system.
 *
 * @param phiMatrix         State transition matrix.
 * @param previousState     Previous system state vector. It is
 *                          the last computed xhat.
 * @param qMatrix    Process noise covariance matrix.
 *
 * @return
 *  0 if OK
 *  -1 if problems arose
 */
int KalmanFilter::Predict(const MatrixXd &phiMatrix,
                          const VectorXd &previousState,
                          const MatrixXd &qMatrix)
noexcept(false) {

    // Create dummy matrices and vectors and call the full
    // Predict() method
    int stateRow(previousState.size());

    MatrixXd dummyControMatrix = MatrixXd::Zero(stateRow, 1);
    VectorXd dummyControlInput = VectorXd::Zero(1);

    return (Predict(phiMatrix,
                    previousState,
                    dummyControMatrix,
                    dummyControlInput,
                    qMatrix));

}  // End of method 'KalmanFilter::Predict()'



// Predicts (or "time updates") the a priori estimate of the system
// state, as well as the a priori estimate error covariance matrix.
//
// @param phiMatrix         State transition matrix.
// @param previousState     Previous system state vector. It is the
//                          last computed xhat.
// @param controlMatrix     Control matrix.
// @param controlInput      Control input vector.
// @param qMatrix           Process noise covariance matrix.
//
// @return
//  0 if OK
//  -1 if problems arose
//
int KalmanFilter::Predict(const MatrixXd &phiMatrix,
                          const VectorXd &previousState,
                          const MatrixXd &controlMatrix,
                          const VectorXd &controlInput,
                          const MatrixXd &qMatrix)
noexcept(false) {
    // After checking sizes, lets' do the real prediction work
    try {


        // Compute the a priori state vector
        xhatminus = phiMatrix * xhat + controlMatrix * controlInput;

        MatrixXd phiT = phiMatrix.transpose();

        // Compute the a priori estimate error covariance matrix
        Pminus = phiMatrix * P * phiT + qMatrix;

    }
    catch (...) {
        InvalidSolver e("Predict(): Unable to predict next state.");
        throw (e);
        return -1;
    }

    return 0;

}  // End of method 'KalmanFilter::Predict()'



// Corrects (or "measurement updates") the a posteriori estimate of
// the system state vector, as well as the a posteriori estimate error
// covariance matrix, using as input the predicted a priori state vector
// and error covariance matrix, plus mVector and associated
// matrices.
//
// @param mVector      Measurements vector.
// @param hMatrix    Measurements matrix. Called geometry
//                              matrix in GNSS.
// @param wMatrix   Measurements weight matrix.
//
// @return
//  0 if OK
//  -1 if problems arose
//
int KalmanFilter::Correct(const VectorXd &mVector,
                          const MatrixXd &hMatrix,
                          const MatrixXd &wMatrix)
noexcept(false) {

    // After checking sizes, let's do the real correction work
    MatrixXd invPMinus;
    MatrixXd hMatrixT = hMatrix.transpose();

    try {
        invPMinus = Pminus.inverse();

        if (debug) {
            cout << "invPMinus" << endl;
            cout << invPMinus << endl;
        }

        MatrixXd hTw = hMatrixT * wMatrix;
        MatrixXd invTemp(hTw * hMatrix + invPMinus);

        if (debug) {
            cout << "invTemp" << endl;
            cout << invTemp << endl;
        }

        // Compute the a posteriori error covariance matrix
        P = invTemp.inverse();

        if (debug) {
            cout << "P" << endl;
            cout << P << endl;
        }

        if (debug) {
            cout << "hTw* mVector" << endl;
            cout << "hTw" << endl;
            cout << hTw << endl;
            cout << hTw.rows() << " numCol:" << hTw.cols() << endl;
            cout << "mVector" << endl;
            cout << mVector << endl;

            cout << hTw * mVector << endl;

            cout << "invPMinus * xhatminus" << endl;
            cout << invPMinus * xhatminus << endl;
        }

        // Compute the a posteriori state estimation
        xhat = P * ((hTw * mVector) + (invPMinus * xhatminus));

        if (debug) {
            cout << "xhat" << endl;
            cout << xhat << endl;
        }

        // 计算延后残差
        postfitResidual = mVector - hMatrix * xhat;

    }
    catch (std::exception &e) {
        InvalidSolver eis("Correct(): Unable to compute xhat.");
        throw (eis);
    }

    return 0;

}  // End of method 'KalmanFilter::Correct()'



