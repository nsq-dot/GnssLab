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

#include <Eigen/Eigen>

using namespace std;
using namespace Eigen;

// Shows how to utilize MLAMBDA-Eigen
int main()
{

	// Lets assume you have the float ambiguities and corresponding covariance matrix
	// I have a sample here, taken from a GPS-only Relative kinematic processing routine,
	// The ambiguites belong to double-differenced carrier phase observations
	// NOTE : The units are in cycles
	// NOTE : Covariance values are large because it is taken from initial stages of processing

	Eigen::VectorXd floatAmb(6);
    floatAmb.setZero();
	Eigen::MatrixXd floatAmbCov(6, 6);
    floatAmbCov.setZero();

    floatAmb << -9.75792, 22.1086, -1.98908, 3.36186, 23.2148, 7.75073;

    cout << floatAmb << endl;

    floatAmbCov << 
        0.0977961, 0.0161137, 0.0468261, 0.0320695, 0.080857, 0.0376408,
    	0.0161137, 0.0208976, 0.0185378, 0.00290225, 0.0111409, 0.0247762,
    	0.0468261, 0.0185378, 0.0435412, 0.0227732, 0.0383208, 0.0382978,
    	0.0320695, 0.00290225, 0.0227732, 0.0161712, 0.0273471, 0.0154774,
    	0.080857, 0.0111409, 0.0383208, 0.0273471, 0.0672121, 0.0294637,
    	0.0376408, 0.0247762, 0.0382978, 0.0154774, 0.0294637, 0.0392536;

    cout << floatAmbCov << endl;


	// Try Ambiguity Resolution...
    ARLambda AR;
    VectorXd fixedAmb;

    fixedAmb = AR.resolve(floatAmb, floatAmbCov);

    cout << "Float Ambiguity: \t" << floatAmb << "\n";
    cout << "Integer Ambiguity: \t" << fixedAmb << "\n";
    cout << "ratio: \t" << AR.squaredRatio << "\n";

}

