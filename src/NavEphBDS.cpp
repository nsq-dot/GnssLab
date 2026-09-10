#include "NavEphBDS.hpp"
#include "CoordStruct.h"
#include <iomanip>

using namespace std;

void NavEphBDS::printData() const {
    cout << "**********************************************************" << endl;
    cout << "BDS Broadcast Ephemeris Data:" << endl;

    cout << scientific << setprecision(8)
         << "af0:   " << setw(16) << af0 << endl
         << "af1:   " << setw(16) << af1 << endl
         << "af2:   " << setw(16) << af2 << endl;

    cout << "BDSWeek: " << setw(15) << BDSWeek << endl
         << "Toe:    " << setw(15) << Toe << endl
         << "TGD1:   " << setw(15) << TGD1 << endl
         << "TGD2:   " << setw(15) << TGD2 << endl;

    cout << "IODC:   " << setw(15) << IODC << endl
         << "IODE:   " << setw(15) << IODE << endl
         << "URA:    " << setw(15) << URA << endl
         << "SV_health: " << SV_health << endl;
}

// =============================================================
// 北斗星历主计算（自动使用 CGCS2000 + BDT + 正确地球自转）
// =============================================================
Xvt NavEphBDS::svXvt(const CommonTime &t) const {
    Xvt sv;
    Xvt res;
    BDSCGCS2000Ellipsoid ell;

    // ==============================================
    // ✅ 【完备库核心】：自动将输入时间 t 转为 BDT
    // ==============================================
    CommonTime t_bdt = convertTimeSystem(t, TimeSystem::BDT);

    // 用【统一的BDT时间】做差，永远不会报错！
    double tk = t_bdt - ctToe;

    // -------------------
    // 以下是你原有正确算法
    // -------------------
    double A = sqrt_A * sqrt_A;
    double n0 = sqrt(ell.gm() / (A*A*A));

    if (tk > 302400)  tk -= 604800;
    if (tk < -302400) tk += 604800;

    double n = n0 + Delta_n;
    double Mk = M0 + n * tk;
    double twoPI = 2 * PI;
    Mk = fmod(Mk, twoPI);

    double Ek = Mk + ecc * sin(Mk);
    double F, G, delea;
    int loop = 1;

    do {
        F = Mk - (Ek - ecc * sin(Ek));
        G = 1.0 - ecc * cos(Ek);
        delea = F / G;
        Ek += delea;
    } while (fabs(delea) > 1e-11 && loop++ < 20);

    sv.relcorr  = svRelativity(t_bdt);
    sv.clkbias  = svClockBias(t_bdt);
    sv.clkdrift = svClockDrift(t_bdt);

    double q      = sqrt(1.0 - ecc*ecc);
    double sinEk  = sin(Ek);
    double cosEk  = cos(Ek);
    double vk     = atan2(q * sinEk, cosEk - ecc);
    double phi_k  = vk + omega;
    double c2p    = cos(2*phi_k);
    double s2p    = sin(2*phi_k);

    double duk = c2p*Cuc + s2p*Cus;
    double drk = c2p*Crc + s2p*Crs;
    double dik = c2p*Cic + s2p*Cis;

    double uk  = phi_k + duk;
    double rk  = A*(1-ecc*cosEk) + drk;
    double ik  = i0 + dik + IDOT * tk;

    double xip = rk * cos(uk);
    double yip = rk * sin(uk);

    double OMEGA_k = OMEGA_0 + (OMEGA_DOT - ell.angVelocity())*tk
                     - ell.angVelocity() * Toe;

    double so = sin(OMEGA_k);
    double co = cos(OMEGA_k);
    double si = sin(ik);
    double ci = cos(ik);

    sv.x[0] = xip*co - yip*ci*so;
    sv.x[1] = xip*so + yip*ci*co;
    sv.x[2] = yip*si;

    double dek = n * A / rk;
    double dlk = sqrt_A * q * sqrt(ell.gm()) / (rk*rk);
    double div = IDOT - 2.0 * dlk * (Cic * s2p - Cis * c2p);
    double domk = OMEGA_DOT - ell.angVelocity();
    double duv = dlk * (1.0 + 2.0 * (Cus * c2p - Cuc * s2p));
    double drv = A * ecc * dek * sinEk - 2.0 * dlk * (Crc * s2p - Crs * c2p);

    double dxp = drv * cos(uk) - rk * sin(uk) * duv;
    double dyp = drv * sin(uk) + rk * cos(uk) * duv;

    sv.v[0] = dxp * co - xip * so * domk - dyp * ci * so + yip * (si * so * div - ci * co * domk);
    sv.v[1] = dxp * so + xip * co * domk + dyp * ci * co - yip * (si * co * div + ci * so * domk);
    sv.v[2] = dyp * si + yip * ci * div;

    sv.typeTGDData["TGD1"] = this->TGD1;
    sv.typeTGDData["TGD2"] = this->TGD2;
    // GPS标识置0区分
    sv.typeTGDData["TGD"] = 0.0;

    return sv;


}

// =====================================================
// 北斗 GEO 卫星专用
// =====================================================
Xvt NavEphBDS::svXvtGEO(const CommonTime &t) const
{
    Xvt sv;
    BDSCGCS2000Ellipsoid ell;

    // ====================== 1. 时间统一为BDT，计算时间差tk ======================
    CommonTime t_bdt = convertTimeSystem(t, TimeSystem::BDT);
    double tk = t_bdt - ctToe;  // 观测历元到参考历元的时间差

    // ====================== 2. 按照北斗16参数星历，计算轨道参数 ======================
    double A = sqrt_A * sqrt_A;
    double n0 = sqrt(ell.gm() / (A*A*A));
    double n = n0 + Delta_n;
    double Mk = M0 + n * tk;

    // 偏近点角迭代
    double Ek = Mk;
    for(int i=0; i<10; i++){
        Ek = Mk + ecc * sin(Ek);
    }

    double sinEk = sin(Ek);
    double cosEk = cos(Ek);
    double sqrt1e2 = sqrt(1 - ecc*ecc);

    // 真近点角
    double sin_vk = (sqrt1e2 * sinEk) / (1 - ecc*cosEk);
    double cos_vk = (cosEk - ecc) / (1 - ecc*cosEk);
    double vk = atan2(sin_vk, cos_vk);

    // 纬度幅角
    double phi_k = vk + omega;

    // 改正项
    double du = Cuc * cos(2*phi_k) + Cus * sin(2*phi_k);
    double dr = Crc * cos(2*phi_k) + Crs * sin(2*phi_k);
    double di = Cic * cos(2*phi_k) + Cis * sin(2*phi_k);

    double u_k = phi_k + du;
    double r_k = A*(1 - ecc*cosEk) + dr;
    double i_k = i0 + IDOT*tk + di;

    // 轨道平面内坐标
    double x_orb = r_k * cos(u_k);
    double y_orb = r_k * sin(u_k);

    // ====================== 3. 计算升交点经度（GEO专用公式） ======================
    double Omega_k = OMEGA_0 + OMEGA_DOT * tk - ell.angVelocity() * Toe;

    // ====================== 4. 计算GEO在【自定义坐标系】下的坐标(X_K, Y_K, Z_K) ======================
    double cosO = cos(Omega_k);
    double sinO = sin(Omega_k);
    double cosi = cos(i_k);
    double sini = sin(i_k);

    double X_K = x_orb * cosO - y_orb * cosi * sinO;
    double Y_K = x_orb * sinO + y_orb * cosi * cosO;
    double Z_K = y_orb * sini;

    // ====================== 5. R_X(-5°) 旋转 ======================
    const double geo_rot_angle = -5.0 * M_PI / 180.0;
    double c5 = cos(geo_rot_angle);
    double s5 = sin(geo_rot_angle);

    double X_GK = X_K;
    double Y_GK = Y_K * c5 + Z_K * s5;
    double Z_GK = -Y_K * s5 + Z_K * c5;

    // ====================== 6. 地球自转修正，转换到CGCS2000坐标系 ======================
    double earthRot = ell.angVelocity() * tk;
    double c = cos(earthRot);
    double s = sin(earthRot);

    sv.x[0] = X_GK * c + Y_GK * s;
    sv.x[1] = -X_GK * s + Y_GK * c;
    sv.x[2] = Z_GK;

    // ==============================================
    //  【GEO专用速度计算】
    // ==============================================
    // ========== 1. 轨道平面内速度 ==========
    // 偏近点角速率
    double Ek_dot = n / (1 - ecc*cosEk);
    // 真近点角速率
    double vk_dot = (sqrt1e2 * Ek_dot) / (1 - ecc*cosEk);
    // 纬度幅角速率
    double phi_k_dot = vk_dot;
    // 改正项速率
    double du_dot = -2*Cuc*sin(2*phi_k)*phi_k_dot + 2*Cus*cos(2*phi_k)*phi_k_dot;
    double dr_dot = -2*Crc*sin(2*phi_k)*phi_k_dot + 2*Crs*cos(2*phi_k)*phi_k_dot;
    double di_dot = -2*Cic*sin(2*phi_k)*phi_k_dot + 2*Cis*cos(2*phi_k)*phi_k_dot;

    // 改正后的纬度幅角、地心地距、轨道倾角速率
    double u_k_dot = phi_k_dot + du_dot;
    double r_k_dot = A*ecc*sinEk*Ek_dot + dr_dot;
    double i_k_dot = IDOT + di_dot;

    // 轨道平面内速度
    double x_orb_dot = r_k_dot * cos(u_k) - r_k * sin(u_k) * u_k_dot;
    double y_orb_dot = r_k_dot * sin(u_k) + r_k * cos(u_k) * u_k_dot;

    // ========== 2. 升交点经度速率（GEO专用） ==========
    double Omega_k_dot = OMEGA_DOT;  // GEO: Ω̇_k = Ω̇

    // ========== 3. 自定义坐标系下的速度(X_K_dot, Y_K_dot, Z_K_dot) ==========
    double cosO_dot = -sinO * Omega_k_dot;
    double sinO_dot = cosO * Omega_k_dot;
    double cosi_dot = -sini * i_k_dot;
    double sini_dot = cosi * i_k_dot;

    double X_K_dot = x_orb_dot*cosO + x_orb*cosO_dot
                   - y_orb_dot*cosi*sinO - y_orb*cosi_dot*sinO - y_orb*cosi*sinO_dot;
    double Y_K_dot = x_orb_dot*sinO + x_orb*sinO_dot
                   + y_orb_dot*cosi*cosO + y_orb*cosi_dot*cosO + y_orb*cosi*cosO_dot;
    double Z_K_dot = y_orb_dot*sini + y_orb*sini_dot;

    // ========== 4. R_X(-5°) 旋转速度 ==========
    double X_GK_dot = X_K_dot;
    double Y_GK_dot = Y_K_dot * c5 + Z_K_dot * s5;
    double Z_GK_dot = -Y_K_dot * s5 + Z_K_dot * c5;

    // ========== 5. 地球自转修正速度（R_Z(ω_e t_k) 导数） ==========
    double earthRot_dot = ell.angVelocity();
    double c_dot = -s * earthRot_dot;
    double s_dot = c * earthRot_dot;

    sv.v[0] = X_GK_dot*c + X_GK*c_dot + Y_GK_dot*s + Y_GK*s_dot;
    sv.v[1] = -X_GK_dot*s - X_GK*s_dot + Y_GK_dot*c + Y_GK*c_dot;
    sv.v[2] = Z_GK_dot;

    // ====================== 7. 钟差、相对论修正 ======================
    sv.clkbias = svClockBias(t_bdt);
    sv.clkdrift = svClockDrift(t_bdt);
    sv.relcorr = svRelativity(t_bdt);

    // =========新增TGD存储=========
    sv.typeTGDData["TGD1"] = this->TGD1;
    sv.typeTGDData["TGD2"] = this->TGD2;
    sv.typeTGDData["TGD"] = 0.0;

    return sv;
}