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

#include <iostream>
#include "ARLambda.hpp"


using namespace std;
using namespace Eigen;


// Resolves a float ambiguity vector to integers.
//
// CONTRACT - the caller distinguishes the two outcomes by isFixed(), never by
// the returned vector alone:
//
//   success : returns the fixed (integer) ambiguities and sets squaredRatio to
//             the textbook's Ratio, (8.61), the second-smallest over the
//             smallest residual sum of squares.
//   failure : returns ambFloat UNCHANGED and sets squaredRatio = 0, so
//             isFixed() is false. This covers a dimension mismatch, a
//             covariance that is not positive definite, and a search that runs
//             out of iterations.
//
// The failure path used to have no return statement at all - falling off the
// end of a non-void function is undefined behaviour - and the two failure modes
// below it were mis-signalled as successes. Both are worse than they look in
// RTK: the search's failure branch is the one data with a poor geometry takes,
// and there the old code reported squaredRatio = 9999.9, i.e. the *least*
// trustworthy result advertised itself as the most reliable.
VectorXd ARLambda::resolve(VectorXd &ambFloat,
                           MatrixXd &ambCov) {
    // Reset first: a failed call must not leave the previous epoch's ratio
    // behind for isFixed() to pick up.
    squaredRatio = 0.0;

    // Check input
    if (ambFloat.size() == 0 ||
        ambFloat.size() != ambCov.rows() ||
        ambFloat.size() != ambCov.cols()) {
        cout << "The dimension of input does not match." << endl;
        cout << "Cannot perform Ambiguity Resolution!" << endl;
        return ambFloat;
    }

    MatrixXd F;
    VectorXd S;
    if (lambda(ambFloat, ambCov, F, S, 2) != 0 || S.size() < 2) {
        // No integer candidate set - see the contract above.
        return ambFloat;
    }

    VectorXd ambFixed = VectorXd::Zero(ambFloat.size());
    for (int i = 0; i < ambFloat.size(); i++) {
        ambFixed(i) = F(i, 0);
    }

    // S is sorted ascending by search(), so S(0) is the best candidate and S(1)
    // the runner-up. When only one candidate was found S(1) stays 0, giving
    // ratio 0 and hence "not fixed", which is the honest answer.
    squaredRatio = (S(0) < 1e-12) ? 9999.9 : S(1) / S(0);
    return ambFixed;
}


int ARLambda::factorize(MatrixXd &Q, MatrixXd &L, VectorXd &D) {
    const int n = static_cast<int>(Q.rows());
    MatrixXd QC = MatrixXd::Zero(Q.rows(), Q.cols());
    QC = Q;
    L = MatrixXd::Zero(n, n);
    D = VectorXd::Zero(n);

    for (int i = n - 1; i >= 0; i--) {
        D(i) = QC(i, i);
        if (D(i) <= 0.0) return -1;
        double temp = std::sqrt(D(i));
        for (int j = 0; j <= i; j++) L(i, j) = QC(i, j) / temp;
        for (int j = 0; j <= i - 1; j++) {
            for (int k = 0; k <= j; k++) QC(j, k) -= L(i, k) * L(i, j);
        }
        for (int j = 0; j <= i; j++) L(i, j) /= L(i, i);
    }

    return 0;
}

void ARLambda::gauss(MatrixXd &L, MatrixXd &Z, int i, int j) {
    const int n = L.rows();
    const int mu = (int) round(L(i, j));
    if (mu != 0) {
        for (int k = i; k < n; k++) L(k, j) -= (double) mu * L(k, i);
        for (int k = 0; k < n; k++) Z(k, j) -= (double) mu * Z(k, i);
    }
}

void ARLambda::permute(MatrixXd &L, VectorXd &D, int j, double del, MatrixXd &Z) {
    const int n = L.rows();
    double eta = D(j) / del;
    double lam = D(j + 1) * L(j + 1, j) / del;

    // *Changed D[j] to D(j) : [] may refer to row slice

    D(j) = eta * D(j + 1);
    D(j + 1) = del;
    for (int k = 0; k <= j - 1; k++) {
        double a0 = L(j, k);
        double a1 = L(j + 1, k);
        L(j, k) = -L(j + 1, j) * a0 + a1;
        L(j + 1, k) = eta * a0 + lam * a1;
    }
    L(j + 1, j) = lam;
    for (int k = j + 2; k < n; k++) swap(L(k, j), L(k, j + 1));
    for (int k = 0; k < n; k++) swap(Z(k, j), Z(k, j + 1));
}

void ARLambda::reduction(MatrixXd &L, VectorXd &D, MatrixXd &Z) {
    const int n = L.rows();
    int j(n - 2), k(n - 2);

    while (j >= 0) {
        if (j <= k) {
            for (int i = j + 1; i < n; i++) {
                gauss(L, Z, i, j);
            }
        }

        double del = D(j) + L(j + 1, j) * L(j + 1, j) * D(j + 1);

        if (del + 1E-6 < D(j + 1)) {
            permute(L, D, j, del, Z);
            k = j;
            j = n - 2;
        } else {
            j--;
        }
    }
}

int ARLambda::search(MatrixXd &L, VectorXd &D, VectorXd &zs, MatrixXd &zn, VectorXd &s, const int &m) {
    // TODO: CHECK UNEXPECTED INPUT
    // n - number of float parameters
    // m - number of fixed solutions
    // L - nxn
    // D - nx1
    // zs - nxn
    // zn - nxm
    // s  - m
    const int LOOPMAX = 10000;
    const int n = L.rows();

    zn = MatrixXd::Zero(n, m);
    s = VectorXd::Zero(m);

    MatrixXd S = MatrixXd::Zero(n, n);
    VectorXd dist = VectorXd::Zero(n);
    VectorXd zb = VectorXd::Zero(n);
    VectorXd z = VectorXd::Zero(n);
    VectorXd step = VectorXd::Zero(n);

    int k = n - 1;
    dist[k] = 0.0;
    zb(k) = zs(k);
    z(k) = round(zb(k));
    double y = zb(k) - z(k);
    step(k) = sign(y);

    int nn(0), imax(0);
    double maxdist = 1E99;
    // `c` has to be the same variable the guard below tests. It used to be
    // redeclared in the for-statement, which shadowed this one, so `c` stayed 0
    // and `if (c >= LOOPMAX) return -1` could never fire: a search that really
    // did exhaust its iteration budget returned 0 ("success") carrying whatever
    // candidates it happened to have.
    int c = 0;
    for (c = 0; c < LOOPMAX; c++) {
        double newdist = dist(k) + y * y / D(k);
        if (newdist < maxdist) {
            if (k != 0) {
                dist(--k) = newdist;
                for (int i = 0; i <= k; i++) {
                    S(k, i) = S(k + 1, i) + (z(k + 1) - zb(k + 1)) * L(k + 1, i);
                }
                zb(k) = zs(k) + S(k, k);
                z(k) = round(zb(k));
                y = zb(k) - z(k);
                step(k) = sign(y);
            } else {
                if (nn < m) {
                    if (nn == 0 || newdist > s(imax)) imax = nn;
                    for (int i = 0; i < n; i++) zn(i, nn) = z(i);
                    s(nn++) = newdist;
                } else {
                    if (newdist < s(imax)) {
                        for (int i = 0; i < n; i++) zn(i, imax) = z(i);
                        s(imax) = newdist;
                        for (int i = imax = 0; i < m; i++) if (s(imax) < s(i)) imax = i;
                    }
                    maxdist = s(imax);
                }
                z(0) += step(0);
                y = zb(0) - z(0);
                step(0) = -step(0) - sign(step(0));
            }
        } else {
            if (k == n - 1) break;
            else {
                k++;
                z(k) += step(k);
                y = zb(k) - z(k);
                step(k) = -step(k) - sign(step(k));
            }
        }
    }
    for (int i = 0; i < m - 1; i++) {
        for (int j = i + 1; j < m; j++) {
            if (s(i) < s(j)) continue;
            swap(s(i), s(j));
            for (k = 0; k < n; k++) swap(zn(k, i), zn(k, j));
        }
    }

    if (c >= LOOPMAX) {
        return -1;
    }

    return 0;
}

// 0 on success, -1 on any failure. The caller must check.
//
// Every failure used to be swallowed: a non-positive-definite covariance
// (factorize() != 0) and a search that gave up (search() != 0) both fell
// through to `return 0` with F never written - F is an uninitialised MatrixXd
// in the caller - and with `s` left as whatever search() had put there. That is
// how a failed search came out as ratio 9999.9.
int ARLambda::lambda(VectorXd &a, MatrixXd &Q, MatrixXd &F, VectorXd &s, const int &m) {
    if ((a.size() != Q.rows()) || (Q.rows() != Q.cols())) return -1;
    if (m < 1) return -1;

    const int n = static_cast<int>(a.size());
    if (n < 1) return -1;

    MatrixXd L = MatrixXd::Zero(n, n);
    MatrixXd E = MatrixXd::Zero(n, m);

    VectorXd D = VectorXd::Zero(n);
    VectorXd z = VectorXd::Zero(n);
    MatrixXd Z = MatrixXd::Identity(n, n);

    // Q = L'*diag(D)*L. Fails when Q is not positive definite, which is what a
    // rank-deficient or otherwise unusable covariance looks like here.
    if (factorize(Q, L, D) != 0) return -1;

    reduction(L, D, Z);
    z = Z.transpose() * a;

    if (search(L, D, z, E, s, m) != 0) return -1;

    try {
        // F=Z'\E - Z nxn  E nxm F nxm
        F = (Z.transpose().inverse()) * E;
    }
    catch (...) {
        return -1;
    }

    return 0;
}


