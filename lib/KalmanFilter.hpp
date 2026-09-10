
/**
 * @file KalmanFilter.hpp
 * Class to compute the solution using a Kalman filter.
 */

#ifndef KalmanFilter_HPP
#define KalmanFilter_HPP

#include "Exception.h"
#include <Eigen/Eigen>

using namespace Eigen;

/** This class computes the solution using a Kalman filter.
 *
 * A typical way to use this class follows:
 *
 * @code
 *       // Declarations and initializations here...
 *    KalmanFilter kalman(xhat0, pmatrix);
 *
 *       // Call this method to reset the filter
 *    kalman.Reset(initialValue,initialErrorVariance);
 *
 *       // One time time update
 *    kalman.TimeUpdate(phiMatrix, qMatrix );4114
 *
 *       // Following several times measurement update
 *    kalman.MeasUpdate( mVector1, hMatrix1, mVectorCovariance1);
 *
 *    kalman.MeasUpdate( mVector2, hMatrix2, mVectorCovariance2);
 *
 *      // Get the final solution
 *    VectorXd x = kalman.xwhat;
 *    MatrixXd p = kalman.P;
 *
 * @endcode
 *
 * More information about the Kalman filter may be found in the
 * excellent and easy introduction by Welch, G. and G. Bishop.
 * "An Introduction to the Kalman Filter", at:
 * http://www.cs.unc.edu/~welch/kalman/kalmanIntro.html.
 *
 *
 */

class KalmanFilter {
public:

    /// Default constructor.
    KalmanFilter() {};

    /** Common constructor.
     *
     * @param initialState     Vector setting the initial state of
     *                         the system.
     * @param initialErrorCovariance    Matrix setting the initial
     *                values of the a posteriori error covariance.
     */
    KalmanFilter(const VectorXd &initialState,
                 const MatrixXd &initialErrorCovariance)
            : xhat(initialState),
              P(initialErrorCovariance) {
        xhatminus = VectorXd::Zero(initialState.size()),
                Pminus = MatrixXd::Zero(initialErrorCovariance.rows(), initialErrorCovariance.cols());
    }


    /** Reset method.
     *
     * This method will reset the filter, setting new values for initial
     * system state vector and the a posteriori error covariance matrix.
     *
     * @param initialState      Vector setting the initial state of
     *                          the system.
     * @param initialErrorCovariance    Matrix setting the initial
     *                   values of the a posteriori error covariance.
     */
    virtual void Reset(const VectorXd &initialState,
                       const MatrixXd &initialErrorCovariance);


    /** Compute the a posteriori estimate of the system state, as well
     *  as the a posteriori estimate error covariance matrix. This
     *  version assumes that no control inputs act on the system.
     *
     * @param phiMatrix         State transition matrix.
     * @param qMatrix    Process noise covariance matrix.
     * @param mVector      Measurements vector.
     * @param hMatrix    Measurements matrix. Called geometry
     *                              matrix in GNSS.
     * @param wMatrix   Measurements noise covariance
     *                                      matrix.
     *
     * @return
     *  0 if OK
     *  -1 if problems arose
     */
    virtual int Compute(const MatrixXd &phiMatrix,
                        const MatrixXd &qMatrix,
                        const VectorXd &mVector,
                        const MatrixXd &hMatrix,
                        const MatrixXd &wMatrix)
    noexcept(false);


    /** Predicts (or "time updates") the a priori estimate of the
        *  system state, as well as the a priori estimate error
        *  covariance matrix.
        *  This version assumes that no control inputs act on the system.
        *
        * @param phiMatrix         State transition matrix.
        * @param qMatrix    Process noise covariance matrix.
        *
        * @return
        *  0 if OK
        *  -1 if problems arose
        */
    virtual int TimeUpdate(const MatrixXd &phiMatrix,
                           const MatrixXd &qMatrix)
    noexcept(false) { return Predict(phiMatrix, xhat, qMatrix); }


    /** Corrects (or "measurement updates") the a posteriori estimate
     *  of the system state vector, as well as the a posteriori estimate
     *  error covariance matrix, using as input the predicted a priori
     *  state vector and error covariance matrix, plus mVector and
     *  associated matrices.
     *
     * @param mVector      Measurements vector.
     * @param hMatrix    Measurements matrix. Called geometry
     *                              matrix in GNSS.
     * @param wMatrix   Measurements noise covariance
     *                                      matrix.
     *
     * @return
     *  0 if OK
     *  -1 if problems arose
     */
    virtual int MeasUpdate(const VectorXd &mVector,
                           const MatrixXd &hMatrix,
                           const MatrixXd &wMatrix)
    noexcept(false) {
        return Correct(mVector,
                       hMatrix,
                       wMatrix);
    }

    virtual int MeasUpdate(const VectorXd &mVector,
                           const MatrixXd &hMatrix,
                           const MatrixXd &wMatrix,
                           const VectorXd &mVectorAug,
                           const MatrixXd &hMatrixAug,
                           const MatrixXd &wMatrixAug
    )
    noexcept(false) {

        ///// 首先做矩阵的增广
        int numMeas = mVector.size();
        int numUnks = hMatrix.cols();
        int numAug = mVectorAug.size();

        VectorXd mVectorExt;
        MatrixXd hMatrixExt;
        MatrixXd wMatrixExt;

        int numMeasExt = numMeas + numAug;

        mVectorExt.setZero(numMeasExt);
        mVectorExt.head(numMeas) = mVector;
        mVectorExt.tail(numAug) = mVectorAug;

        hMatrixExt.setZero(numMeasExt, numUnks);
        hMatrixExt.block(0, 0, numMeas, numUnks) = hMatrix;
        hMatrixExt.block(numMeas, 0, numAug, numUnks) = hMatrixAug;

        wMatrixExt.setZero(numMeasExt, numMeasExt);
        wMatrixExt.block(0, 0, numMeas, numMeas) = wMatrix;
        wMatrixExt.block(numMeas, numMeas, numAug, numAug) = wMatrixAug;

        // 利用增广后的观测方程求解最小二乘解hMatrixAug
        return Correct(mVectorExt, hMatrixExt, wMatrixExt);

    }


    /// Destructor.
    virtual ~KalmanFilter() {};

    /// A posteriori state estimation. This is usually your target.
    VectorXd xhat;

    /// A posteriori error covariance.
    MatrixXd P;

    /// A priori state estimation.
    VectorXd xhatminus;

    /// A priori error covariance.
    MatrixXd Pminus;

    // postfitResidual
    VectorXd postfitResidual;

private:

    /** Predicts (or "time updates") the a priori estimate of the
     *  system state, as well as the a priori estimate error
     *  covariance matrix.
     *  This version assumes that no control inputs act on the system.
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
    virtual int Predict(const MatrixXd &phiMatrix,
                        const VectorXd &previousState,
                        const MatrixXd &qMatrix)
    noexcept(false);

    virtual int Predict(const MatrixXd &phiMatrix,
                        const VectorXd &previousState,
                        const MatrixXd &controlMatrix,
                        const VectorXd &controlInput,
                        const MatrixXd &qMatrix)
    noexcept(false);

    /** Corrects (or "measurement updates") the a posteriori estimate
     *  of the system state vector, as well as the a posteriori estimate
     *  error covariance matrix, using as input the predicted a priori
     *  state vector and error covariance matrix, plus mVector and
     *  associated matrices.
     *
     * @param mVector      Measurements vector.
     * @param hMatrix    Measurements matrix. Called geometry
     *                              matrix in GNSS.
     * @param wMatrix   Measurements weight matrix.
     *
     * @return
     *  0 if OK
     *  -1 if problems arose
     */
    virtual int Correct(const VectorXd &mVector,
                        const MatrixXd &hMatrix,
                        const MatrixXd &wMatrix)
    noexcept(false);


}; // End of class 'KalmanFilter'


#endif // KalmanFilter_HPP
