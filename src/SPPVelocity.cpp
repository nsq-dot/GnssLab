#include "SPPVelocity.h"
#include <vector>
#include <algorithm>

bool SPPVelocity::solveVelocityEpoch(const Eigen::Vector3d& recXYZ,
                                     ObsData& obsData,
                                     std::map<SatID, Xvt>& satXvtRecTime,
                                     SatValueMap& satElevData,
                                     Eigen::Vector3d& velXYZOut,
                                     double& clkDotOut)
{
    velEquSys = EquSys();
    std::map<SatID, double> dopObsMap;
    std::map<SatID, double> satLambda;

    // 1. 提取合格多普勒观测
    dopObsMap = extractValidDoppler(obsData, satXvtRecTime, satElevData, satLambda);
    if (dopObsMap.size() < 4)
    {
#if VELOCITY_DEBUG
        std::cout << "[Vel Warn] Valid Doppler Sat Num = " << dopObsMap.size()
                  << " < 4, skip velocity" << std::endl;
#endif
        return false;
    }

    // 2. 构建多普勒线性观测方程
    linearizeVelocity(recXYZ, obsData, dopObsMap, satLambda, satXvtRecTime, satElevData);

    // 3. 最小二乘求解并提取速度、钟漂
    bool solveSuccess = solveLSQGetResult(velXYZOut, clkDotOut);
    if (!solveSuccess)
    {
#if VELOCITY_DEBUG
        std::cout << "[Vel Warn] LSQ solve failed" << std::endl;
#endif
        return false;
    }
    return true;
}

std::map<SatID, double> SPPVelocity::extractValidDoppler(ObsData& obsData,
                                                         std::map<SatID, Xvt>& satXvtRecTime,
                                                         SatValueMap& satElevData,
                                                         std::map<SatID, double>& lambdaMap)
{
    std::map<SatID, double> dopMap;
    // 多普勒在 RinexObsReader 里被单独存进了 obsData.satDopplerData，
    // key 是完整观测类型，例如 GPS 的 "D1W"、北斗的 "D2I"。
    for (auto& sv : obsData.satDopplerData)
    {
        SatID sat = sv.first;
        // 高度角过滤
        if (!satElevData.count(sat) || satElevData[sat] < cutOffElev)
            continue;
        // 无卫星XVT直接跳过（意味着星历/伪距定位阶段已剔除该卫星）
        if (!satXvtRecTime.count(sat))
            continue;

        std::string sys = sat.system;
        std::string pref;   // 参与测速的频点前缀
        double freq = 0.0;
        if (sys == "G")                 { pref = "D1"; freq = getFreq("G", "C1"); } // L1
        else if (sys == "C")            { pref = "D2"; freq = getFreq("C", "C2"); } // B1I
        else continue;
        if (freq <= 0.0) continue;

        // 挑该卫星上匹配频点的第一个多普勒观测
        for (const auto& tv : sv.second)
        {
            if (tv.first.compare(0, pref.size(), pref) == 0 && std::fabs(tv.second) > 1e-12)
            {
                dopMap[sat] = tv.second;
                lambdaMap[sat] = C_MPS / freq;
                break;
            }
        }
    }
    return dopMap;
}

void SPPVelocity::linearizeVelocity(const Eigen::Vector3d& recXYZ,
                                    ObsData& obsData,
                                    std::map<SatID, double>& dopMap,
                                    std::map<SatID, double>& lambdaMap,
                                    std::map<SatID, Xvt>& satXvtRecTime,
                                    SatValueMap& satElevData)
{
    // 严格匹配Parameter枚举大写 dVX dVY dVZ cdtDot
    Variable dVx(obsData.station, Parameter::dVX);
    Variable dVy(obsData.station, Parameter::dVY);
    Variable dVz(obsData.station, Parameter::dVZ);
    Variable cdtDot(obsData.station, Parameter::cdtDot);
    VariableSet varSet;
    varSet.insert(dVx);
    varSet.insert(dVy);
    varSet.insert(dVz);
    varSet.insert(cdtDot);   // 【必须】钟漂也要进未知参数集合，否则列号越界

    for (auto& entry : dopMap)
    {
        SatID sat = entry.first;
        double Dobs = entry.second;
        double lam = lambdaMap[sat];
        Xvt satXvt = satXvtRecTime[sat];
        Eigen::Vector3d satPos = satXvt.x;
        Eigen::Vector3d satVel = satXvt.v;

        // 接收机-卫星矢量
        Eigen::Vector3d deltaR = satPos - recXYZ;
        double rho = deltaR.norm();
        Eigen::Vector3d e = deltaR / rho;

        // 多普勒观测方程： l = -λD - e·Vs + c·satClockDrift = -e·Vr + c·clkDot
        double satClkDrift = satXvt.clkdrift;   // 单位 s/s
        double l = -lam * Dobs - e.dot(satVel) + C_MPS * satClkDrift;

        EquID equId(sat, "DOP");
        EquData equ;
        equ.prefit = l;

        equ.varCoeffData[dVx] = -e(0);
        equ.varCoeffData[dVy] = -e(1);
        equ.varCoeffData[dVz] = -e(2);
        equ.varCoeffData[cdtDot] = 1.0;   // 未知数取 c*δṫ_r，单位 m/s

        // 高度角加权
        double elevDeg = satElevData[sat];
        double elevRad = elevDeg * DEG_TO_RAD;
        double weight = 1.0 / (sigDoppler * sigDoppler);
        if (elevDeg < 30)
            weight *= std::pow(std::sin(elevRad), 2);
        equ.weight = weight;

        velEquSys.obsEquData[equId] = equ;
    }
    velEquSys.varSet = varSet;
}

int SPPVelocity::indexOfVar(const VariableSet& varSet, const Variable& v)
{
    int idx = 0;
    for (auto var : varSet)
    {
        if (var == v) return idx;   // Variable::operator== 非 const，故按值拷贝
        idx++;
    }
    return -1;
}

bool SPPVelocity::solveLSQGetResult(Eigen::Vector3d& velXYZOut, double& clkDotOut)
{
    // SolverLSQ 的实现里 solve() 结尾固定去取 dX/dY/dZ，并不适用测速未知参数集合，
    // 而且它的解向量 state 是 private 无法取出。所以这里直接在类内按加权最小二乘做，
    // 未知参数顺序 = velEquSys.varSet 的排列顺序。
    auto solveOnce = [&](Eigen::VectorXd& deltaOut, double& sig0) -> bool {
        int numUnk = (int)velEquSys.varSet.size();
        int numObs = (int)velEquSys.obsEquData.size();
        if (numUnk == 0 || numObs < numUnk) return false;

        Eigen::VectorXd prefit(numObs);
        Eigen::MatrixXd hMat = Eigen::MatrixXd::Zero(numObs, numUnk);
        Eigen::VectorXd wVec(numObs);

        int iobs = 0;
        for (auto& ed : velEquSys.obsEquData)
        {
            prefit(iobs) = ed.second.prefit;
            wVec(iobs)   = ed.second.weight;
            for (auto& vc : ed.second.varCoeffData)
            {
                int idx = indexOfVar(velEquSys.varSet, vc.first);
                if (idx < 0 || idx >= numUnk) return false;
                hMat(iobs, idx) = vc.second;
            }
            iobs++;
        }

        Eigen::MatrixXd W = wVec.asDiagonal();
        Eigen::MatrixXd N = hMat.transpose() * W * hMat;
        Eigen::VectorXd b = hMat.transpose() * W * prefit;
        Eigen::FullPivLU<Eigen::MatrixXd> lu(N);
        if (!lu.isInvertible()) return false;
        deltaOut = lu.inverse() * b;

        // 单位权中误差
        Eigen::VectorXd r = prefit - hMat * deltaOut;
        double vv = r.dot(W * r);
        sig0 = (numObs > numUnk) ? std::sqrt(vv / (numObs - numUnk)) : 0.0;
        return true;
    };

    // 迭代：解算->按验后残差剔除粗差->重解（粗差卫星的多普勒会污染速度解）
    // 剔除判据用稳健 sigma（残差绝对值中位数*1.4826），单位与残差同为 m/s；
    // 不能用 solveOnce 的 sig0(单位权)直接当阈值：权重 w=1/sigDoppler^2 下它比
    // m/s 尺度大 sqrt(w) 倍，且会被少数粗差顶大，导致阈值虚高、一颗都删不掉。
    const int maxIter = 6;
    Eigen::VectorXd delta;
    double sig0 = 0.0;
    for (int it = 0; it < maxIter; ++it)
    {
        if (!solveOnce(delta, sig0)) return false;

        // 算各观测验后残差 est = prefit - H*delta，并找最大者
        int numUnk = (int)velEquSys.varSet.size();
        double worst = -1.0;
        EquID worstID;
        std::vector<double> resid;
        resid.reserve(velEquSys.obsEquData.size());
        for (auto& ed : velEquSys.obsEquData)
        {
            double est = ed.second.prefit;
            for (auto& vc : ed.second.varCoeffData)
            {
                int idx = indexOfVar(velEquSys.varSet, vc.first);
                if (idx < 0 || idx >= numUnk) { est = 0.0; break; }
                est -= vc.second * delta(idx);
            }
            resid.push_back(est);                 // 有符号残差，单位 m/s
            double rr = std::fabs(est);
            if (rr > worst) { worst = rr; worstID = ed.first; }
        }

        // 稳健 sigma：MAD = median(|r|)（残差已中心化，≈0），*1.4826 得正态σ
        std::vector<double> absr(resid.size());
        for (size_t k = 0; k < resid.size(); ++k) absr[k] = std::fabs(resid[k]);
        double med = 0.0;
        if (!absr.empty())
        {
            std::sort(absr.begin(), absr.end());
            med = absr[absr.size() / 2];
        }
        double sigRob = 1.4826 * med;
        double sigUse = std::max(sigRob, sigDoppler);   // 不低于量测噪声 0.05 m/s
        double thresh = 3.0 * sigUse;
        if (worst <= thresh) break;
        velEquSys.obsEquData.erase(worstID);   // 剔除该观测再解
    }

    // 从解向量取速度/钟漂
    int k = 0;
    bool found[4] = {false,false,false,false};
    for (auto& var : velEquSys.varSet)
    {
        switch (var.getParaType())
        {
            case Parameter::dVX:    velXYZOut(0) = delta(k); found[0]=true; break;
            case Parameter::dVY:    velXYZOut(1) = delta(k); found[1]=true; break;
            case Parameter::dVZ:    velXYZOut(2) = delta(k); found[2]=true; break;
            case Parameter::cdtDot: clkDotOut    = delta(k); found[3]=true; break;
            default: break;
        }
        k++;
    }
    return found[0] && found[1] && found[2] && found[3];
}

// ECEF速度转站心ENU，复用CoordConvert已实现ecef2enu
Eigen::Vector3d SPPVelocity::xyz2Enu(const Eigen::Vector3d& recXYZ, const Eigen::Vector3d& velXYZ)
{
    WGS84 wgs84Frame;
    XYZ recXyzStruct(recXYZ(0), recXYZ(1), recXYZ(2));
    // ecef2enu(ref, sat) 内部求 (sat-ref)。ENU 是速度向量的旋转，
    // 平移应为 0，故把 "卫星点" 取为 recXYZ+velXYZ，这样 sat-ref = velXYZ，
    // 而旋转基准 B/L 仍是测站的。直接传 velXYZ 会错减一次测站坐标。
    XYZ velAsPos(recXYZ(0) + velXYZ(0),
                 recXYZ(1) + velXYZ(1),
                 recXYZ(2) + velXYZ(2));
    Eigen::Vector3d velENU = ecef2enu(recXyzStruct, velAsPos, wgs84Frame);
    return velENU;
}
