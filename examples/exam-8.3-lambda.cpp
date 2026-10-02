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

// Textbook chapter 8.3.5 (ambiguity fixing) - MLAMBDA on a float ambiguity
// vector and its covariance, followed by the ratio test of (8.61).
//
// Three cases, chosen so that the three outcomes of ARLambda::resolve() are all
// visible. tests/test_lambda_resolve.py asserts every number printed here, and
// needs no dataset and no other program - which is why this is built as a real
// target rather than left as a source file nothing compiles (it was, until the
// chapter-8 exercise-2 work needed it).
//
//   case 1  the lecture notes' own example 8-1. Fixes to [-9 21 -2 4 24 7],
//           ratio 6.51682, which clears the threshold of 3.
//   case 2  a deliberately marginal pair: several integer candidates have the
//           same residual sum of squares, so the ratio is 1 and isFixed() is
//           false. Note what does and does not happen here - the search itself
//           succeeded, so an integer candidate still comes back; rejecting it
//           is the CALLER's decision via isFixed(). Which of the tied
//           candidates is returned is arbitrary, which is precisely what the
//           ratio test exists to catch.
//   case 3  a covariance that is not positive definite - rank 1, as a
//           rank-deficient epoch produces. There is no integer search to run at
//           all, so resolve() hands back the float solution unchanged and
//           reports ratio 0. This is the one case where the float vector is
//           what comes back. The *old* code segfaulted here (and, with Eigen's
//           assertions off, silently read uninitialised memory and announced
//           ratio 9999.9 - see docs/rtk.md).

#include <iostream>
#include <iomanip>
#include <string>

#include <Eigen/Eigen>
#include "ARLambda.hpp"

using namespace std;
using namespace Eigen;

namespace {

// The textbook's threshold for the ratio test, (8.61). 2 and 3 are both used in
// practice; the notes take 3.
const double kRatioThreshold = 3.0;

void printVector(const string &label, const VectorXd &v) {
    cout << left << setw(9) << label << ":";
    cout << right;
    for (int i = 0; i < v.size(); ++i)
        cout << " " << setw(16) << v(i);
    cout << "\n";
}

// Takes the inputs by value: ARLambda::resolve() wants non-const references
// (it is free to hand the float vector straight back), and the copies keep the
// call sites above readable.
void report(const string &title, VectorXd ambFloat, MatrixXd ambCov) {
    cout << "\n=== " << title << " ===\n";

    ARLambda ar;
    VectorXd resolved = ar.resolve(ambFloat, ambCov);

    cout << setprecision(12) << fixed;
    printVector("float", ambFloat);
    printVector("resolved", resolved);

    cout << setprecision(8);
    cout << left << setw(9) << "ratio" << ": " << ar.squaredRatio << "\n";
    cout << left << setw(9) << "fixed" << ": "
         << (ar.isFixed(kRatioThreshold) ? "yes" : "no")
         << "   (ratio threshold " << kRatioThreshold << ")\n";
}

}  // namespace

// Resolves float ambiguities with MLAMBDA-Eigen.
int main() {

    cout << "MLAMBDA ambiguity resolution - textbook 8.3.5, example 8-1\n";

    // ------------------------------------------------------------------
    // case 1: example 8-1 of the notes. The sample came from a GPS-only
    // relative kinematic run, early enough that the covariance is still large.
    // ------------------------------------------------------------------
    {
        VectorXd ambFloat(6);
        ambFloat << -9.75792, 22.1086, -1.98908, 3.36186, 23.2148, 7.75073;

        MatrixXd ambCov(6, 6);
        ambCov <<
            0.0977961,  0.0161137,  0.0468261,  0.0320695,  0.080857,   0.0376408,
            0.0161137,  0.0208976,  0.0185378,  0.00290225, 0.0111409,  0.0247762,
            0.0468261,  0.0185378,  0.0435412,  0.0227732,  0.0383208,  0.0382978,
            0.0320695,  0.00290225, 0.0227732,  0.0161712,  0.0273471,  0.0154774,
            0.080857,   0.0111409,  0.0383208,  0.0273471,  0.0672121,  0.0294637,
            0.0376408,  0.0247762,  0.0382978,  0.0154774,  0.0294637,  0.0392536;

        report("case 1: textbook example 8-1, six double-difference ambiguities",
               ambFloat, ambCov);
    }

    // ------------------------------------------------------------------
    // case 2: exactly halfway between two integers, unit covariance. The best
    // two candidates tie, so the ratio is 1 and nothing is fixed.
    // ------------------------------------------------------------------
    {
        VectorXd ambFloat(2);
        ambFloat << 0.5, 2.5;

        MatrixXd ambCov = MatrixXd::Identity(2, 2);

        report("case 2: two candidates tie - the ratio test must reject",
               ambFloat, ambCov);
    }

    // ------------------------------------------------------------------
    // case 3: unusable covariance. Rows 1 and 2 are 0.5x and 0.2x row 0, so the
    // matrix is rank 1 and factorize() fails.
    // ------------------------------------------------------------------
    {
        VectorXd ambFloat(3);
        ambFloat << 1.2, 3.4, -0.7;

        MatrixXd ambCov(3, 3);
        ambCov <<
            1.0, 0.5, 0.2,
            0.5, 0.25, 0.1,
            0.2, 0.1, 0.04;

        report("case 3: covariance is not positive definite - no search possible",
               ambFloat, ambCov);
    }

    return 0;
}
