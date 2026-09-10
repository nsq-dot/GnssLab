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


#include <string>
#include "NavEphGPS.hpp"

using namespace std;

void NavEphGPS::printData() const {
    cout << "****************************************************************"
         << "************" << endl
         << "GPS Broadcast Ephemeris Data: " << endl;
    cout << "Toc: " << this->CivilToc.year << " " << this->CivilToc.month << " "
         << this->CivilToc.day << " " << this->CivilToc.hour << " "
         << this->CivilToc.minute << " " << this->CivilToc.second << endl;
    cout << scientific << setprecision(8)
         << "af0: " << setw(16) << af0 << endl
         << "af1: " << setw(16) << af1 << endl
         << "af2: " << setw(16) << af2 << endl;

    cout << "IODE: " << setw(16) << IODE << endl
         << "Crs:  " << setw(16) << Crs << endl
         << "Delta_n: " << setw(16) << Delta_n << endl
         << "M0: " << setw(16) << M0 << endl;

    cout << "Cuc: " << setw(16) << Cuc << endl
         << "ecc: " << setw(16) << ecc << endl
         << "Cus: " << setw(16) << Cus << endl
         << "sqrt_A: " << setw(16) << sqrt_A << endl;

    cout << "Toe: " << setw(16) << Toe << endl;
    cout << "Cic: " << setw(16) << Cic << endl;
    cout << "OMEGA_0: " << setw(16) << OMEGA_0 << endl;
    cout << "Cis: " << setw(16) << Cis << endl;

    cout << "i0: " << setw(16) << i0 << endl;
    cout << "Crc: " << setw(16) << Crc << endl;
    cout << "omega: " << setw(16) << omega << endl;
    cout << "OMEGA_DOT: " << setw(16) << OMEGA_DOT << endl;

    cout << "IDOT: " << setw(16) << IDOT << endl;
    cout << "Codes_On_L2_Channel: " << setw(16) << L2Codes << endl;
    cout << "GPSWeek: " << setw(16) << GPSWeek << endl;
    cout << "L2P_data_flag: " << setw(16) << L2Pflag << endl;

    cout << "URA: " << setw(16) << URA << endl;
    cout << "SV_health: " << setw(16) << SV_health << endl;
    cout << "TGD: " << setw(16) << TGD << endl;
    cout << "IODC: " << setw(16) << IODC << endl;

    cout << "HOWtime: " << setw(16) << HOWtime << endl;
    cout << "fitInterval: " << setw(16) << fitInterval << endl;

    cout << "ctToc: " << ctToc.toString() << endl;
    cout << "ctToe: " << ctToe.toString() << endl;
}


double NavEphGPS::svClockBias(const CommonTime &t) const {
    double dtc, elaptc;
    elaptc = t - ctToc;
    cout << "elaptc:" <<  elaptc << endl;
    dtc = af0 + elaptc * (af1 + elaptc * af2);
    cout << "af0:" << af0 << "af1:" << af1 << "af2:" << af2 << endl;
    cout << "dtc:" << dtc << endl;
    return dtc;
}

double NavEphGPS::svClockDrift(const CommonTime &t) const {
    double drift, elaptc;
    elaptc = t - ctToc;
    drift = af1 + 2.0 * elaptc * af2;
    return drift;
}

// Compute satellite relativity correction (sec) at the given time
// throw Invalid Request if the required data has not been stored.
double NavEphGPS::svRelativity(const CommonTime &t) const {
    GPSEllipsoid ell;
    ///Semi-major axis
    double A = sqrt_A * sqrt_A;

    ///Computed mean motion (rad/sec)
    double n0 = std::sqrt(ell.gm() / (A * A * A));

    ///Time from ephemeris reference epoch
    double tk = t - ctToe;
    if (tk > 302400) tk = tk - 604800;
    if (tk < -302400) tk = tk + 604800;

    ///Corrected mean motion
    double n = n0 + Delta_n;

    ///Mean anomaly
    double Mk = M0 + n * tk;

    ///Kepler's Equation for Eccentric Anomaly
    ///solved by iteration
    double twoPI = 2.0e0 * PI;
    Mk = fmod(Mk, twoPI);
    double Ek = Mk + ecc * ::sin(Mk);
    int loop_cnt = 1;
    double F, G, delea;
    do {
        F = Mk - (Ek - ecc * ::sin(Ek));
        G = 1.0 - ecc * ::cos(Ek);
        delea = F / G;
        Ek = Ek + delea;
        loop_cnt++;
    } while ((fabs(delea) > 1.0e-11) && (loop_cnt <= 20));

    return (REL_CONST * ecc * std::sqrt(A) * ::sin(Ek));
}

double NavEphGPS::svURA(const CommonTime &t) const {
    double ephURA = URA;
    return ephURA;
}

Xvt NavEphGPS::svXvt(const CommonTime &t) const {
    Xvt sv;
    GPSEllipsoid ell;

    // Semi-major axis
    double A = sqrt_A * sqrt_A;

    // Computed mean motion (rad/sec)
    double n0 = std::sqrt(ell.gm() / (A * A * A));

    // Time from ephemeris reference epoch, corrected to ±302400s
    double tk = t - ctToe;
    if (tk > 302400) tk -= 604800;
    else if (tk < -302400) tk += 604800;

    // Corrected mean motion
    double n = n0 + Delta_n;

    // Mean anomaly
    double Mk = M0 + n * tk;

    // Kepler's Equation for Eccentric Anomaly (iterative solution)
    const double twoPI = 2.0 * PI;
    Mk = fmod(Mk, twoPI);
    double Ek = Mk + ecc * std::sin(Mk);
    int loop_cnt = 1;
    double F, G, delea;
    do {
        F = Mk - (Ek - ecc * std::sin(Ek));
        G = 1.0 - ecc * std::cos(Ek);
        delea = F / G;
        Ek += delea;
        loop_cnt++;
    } while ((std::fabs(delea) > 1.0e-11) && (loop_cnt <= 20));

    // Clock corrections (including relativity and TGD)
    sv.relcorr = svRelativity(t);
    sv.clkbias = svClockBias(t) - TGD;  // 补充群延迟TGD
    sv.clkdrift = svClockDrift(t);

    // True Anomaly
    double q = std::sqrt(1.0 - ecc * ecc);
    double sinEk = std::sin(Ek);
    double cosEk = std::cos(Ek);
    double GSTA = q * sinEk;
    double GCTA = cosEk - ecc;
    double vk = std::atan2(GSTA, GCTA);

    // Argument of Latitude
    double phi_k = vk + omega;
    double cos2phi_k = std::cos(2.0 * phi_k);
    double sin2phi_k = std::sin(2.0 * phi_k);
    double duk = cos2phi_k * Cuc + sin2phi_k * Cus;
    double drk = cos2phi_k * Crc + sin2phi_k * Crs;
    double dik = cos2phi_k * Cic + sin2phi_k * Cis;
    double uk = phi_k + duk;
    double rk = A * (1.0 - ecc * cosEk) + drk;
    double ik = i0 + dik + IDOT * tk;

    // Positions in orbital plane
    double xip = rk * std::cos(uk);
    double yip = rk * std::sin(uk);

    // Corrected longitude of ascending node
    double OMEGA_k = OMEGA_0 + (OMEGA_DOT - ell.angVelocity()) * tk
                     - ell.angVelocity() * Toe;

    // Earth-fixed coordinates (position)
    double sinOMG_k = std::sin(OMEGA_k);
    double cosOMG_k = std::cos(OMEGA_k);
    double cosik = std::cos(ik);
    double sinik = std::sin(ik);
    double xef = xip * cosOMG_k - yip * cosik * sinOMG_k;
    double yef = xip * sinOMG_k + yip * cosik * cosOMG_k;
    double zef = yip * sinik;
    sv.x[0] = xef;
    sv.x[1] = yef;
    sv.x[2] = zef;

    // Velocity derivatives
    double dek = n / (1.0 - ecc * cosEk);
    double dlk = q * dek / (1.0 - ecc * cosEk);
    double di = IDOT - 2.0 * dlk * (Cic * sin2phi_k - Cis * cos2phi_k);
    double dOmega = OMEGA_DOT - ell.angVelocity();
    double du = dlk * (1.0 + 2.0 * (Cus * cos2phi_k - Cuc * sin2phi_k));
    double dr = A * ecc * dek * sinEk - 2.0 * dlk * (Crc * sin2phi_k - Crs * cos2phi_k);
    double dxp = dr * std::cos(uk) - rk * std::sin(uk) * du;
    double dyp = dr * std::sin(uk) + rk * std::cos(uk) * du;

    // Earth-fixed coordinates (velocity)
    double vxef = dxp * cosOMG_k - xip * sinOMG_k * dOmega
                  - dyp * cosik * sinOMG_k + yip * (sinik * sinOMG_k * di - cosik * cosOMG_k * dOmega);
    double vyef = dxp * sinOMG_k + xip * cosOMG_k * dOmega
                  + dyp * cosik * cosOMG_k - yip * (sinik * cosOMG_k * di + cosik * sinOMG_k * dOmega);
    double vzef = dyp * sinik + yip * cosik * di;
    sv.v[0] = vxef;
    sv.v[1] = vyef;
    sv.v[2] = vzef;

    return sv;
}

bool NavEphGPS::isValid(const CommonTime &ct) const {
    if (ct < beginValid || ct > endValid) return false;
    return true;
}


