#include "SPPIFCode.h"
#include "SPPVelocity.h"
#include "CoordConvert.h"
#include <Eigen/Eigen>

#define debug 1

void SPPIFCode::solve(ObsData &obsData) {
    //----------------------
    // 去掉通道号，C1W, C1C => C1;
    // 后面computeSatPos里用与通道号无关的观测值计算卫星发射时刻位置
    //----------------------
    convertObsType(obsData);

    if(debug)
    {
        cout << "after convertObsType" << endl;
        cout << obsData << endl;
    }

    // 计算发射时刻卫星位置（参考框架为时刻的）
    satXvtTransTime = computeSatPos(obsData);
    if(debug)
    {
        cout << "satXvtTransTime" << CommonTime2CivilTime(obsData.epoch) << endl;
        for(auto sx: satXvtTransTime)
        {
            cout << sx.first  << endl;
            cout << sx.second << endl;
        };
    }

    // 新增：TGD硬件延迟改正
    correctTGD(obsData, satXvtTransTime);

    // 计算IF组合
    processObs(obsData);

    //----------------------
    // 得到卫星发射时刻位置和钟差、相对论和TGD后，改正观测值延迟，并更新C1/C2等观测值
    //----------------------

    xyz = obsData.antennaPosition;
    dxyz = {100, 100, 100};

    int iter(0);
    while (true) {

        satXvtRecTime = earthRotation(xyz, satXvtTransTime);

        if(debug)
        {
            cout << "satXvtRecTime" << endl;
            for(auto sx: satXvtRecTime)
            {
                cout << sx.first << " xvt:" << endl;
                cout << sx.second << endl;
            };
        }

        // step 1: 确定观测值和未知参数的纬数
        // 根据数据结构中已经有的satTypePrefitData, satTypeVarCoeffData;
        // 得到numObs, numUnk的数值
        int numSats = obsData.satTypeValueData.size();

        // 这里应该抛出异常，而不是break，因为无法解算，所以后续rtk也不能算，
        // 所以在rtk的主程序里捕获这个异常，然后再continue下一个历元；
        // 如果break了，就不知道问题在哪里了
        if (numSats < 4 ) {
            SVNumException e("num of satellites is less than 4");
            throw(e);
        }


        // 地球表面才计算高度角和大气改正
        if(std::abs(xyz.norm() - RadiusEarth) < 100000.0)
        {
            satElevData.clear();
            satAzimData.clear();
            satTropData.clear(); // 新增清空对流层存储
            if(debug)
                cout << "computeElevAzim" << endl;

            computeElevAzim(xyz, satXvtRecTime,satElevData,satAzimData);

            if(debug)
            {
                cout << "satElevData:" << endl;
                cout << satElevData << endl;
            }

            if(enableTrop)
            {
                computeTropDelay(xyz, satXvtRecTime, satElevData);
            }
        }

        equSys = linearize(xyz, satXvtRecTime, satElevData, obsData);

        if(debug)
            cout << "afte linearize:" << endl;

        // 如果是基准站，完成线性化后就退出
        // 因为基准站位置是准确的
        if(!isRover)
            break;

        solverLsq.solve(equSys);
        dxyz = solverLsq.getdxyz();

        xyz += dxyz;
        cout << "iteration:" << iter
        << "dxyz:" << dxyz.transpose()
        << "xyz:" << xyz.transpose() << endl;

        // convergence threshold
        if (dxyz.norm() < 0.01) {
            break;
        }

        if (iter > 10) {
            InvalidSolver e("too many iterations");
            throw(e);
        }
        iter++;

    }

    result.xyz = xyz;
}

std::map<SatID, Xvt> SPPIFCode::computeSatPos(ObsData &obsData) {
    std::map<SatID, Xvt> satXvtData;
    SatIDSet satRejectedSet;
    CommonTime time = obsData.epoch;
    // Loop through all the satellites
    for (auto stv: obsData.satTypeValueData) {
        SatID sat(stv.first);
        Xvt xvt;
        // compute satellite ephemeris at transmitting time
        // Scalar to hold temporal value
        double obs(0.0);
        string codeType;
        if (sat.system == "G") {
            codeType = "C1";
        }
        // todo
        // 请增加bds或其他系统的观测值选择
        else if (sat.system == "C") {
            codeType = "C2"; // 北斗B1I对应观测码
        }

        else {
            satRejectedSet.insert(sat);
            continue;
        }

        // code obs
        try {
            obs = stv.second.at(codeType);
            if(debug)
                cout << "sat:" << sat << "obs:" << codeType << "value:" << obs << endl;
        }
        catch (...) {
            satRejectedSet.insert(sat);
            continue;
        }

        // now, compute xvt
        try {
            xvt = computeAtTransmitTime(time, obs, sat);
        }
        catch (InvalidRequest &e) {
            satRejectedSet.insert(sat);
            continue;
        }
        satXvtData[sat] = xvt;
    }

    // remove bad sat;
    for (auto sat: satRejectedSet) {
        obsData.satTypeValueData.erase(sat);
    }

    return satXvtData;

};

Xvt SPPIFCode::computeAtTransmitTime(const CommonTime &tr,
                                     const double &pr,
                                     const SatID &sat)
noexcept(false) {
    Xvt xvt;

    CommonTime tt;
    CommonTime transmit = tr;

    transmit -= pr / C_MPS;
    tt = transmit;

    // 这里也可以用while循环来替换这里的迭代次数
    for (int i = 0; i < 2; i++) {
        if (pEphStore != NULL) {
            xvt = pEphStore->getXvt(sat, tt);

        }
        tt = transmit;
        tt -= (xvt.clkbias + xvt.relcorr);
    }
    return xvt;
};

void SPPIFCode::correctTGD(ObsData &obsData, std::map<SatID,Xvt>& satXvtTransTime)
{
    if (!enableBDSTGD)
    {
        cout << "[TGD DEBUG] BDS TGD correction disabled, skip all satellites" << endl;
        return;
    }

    for (auto &stv : obsData.satTypeValueData)
    {
        SatID sat = stv.first;
        if (!satXvtTransTime.count(sat))
        {
            cout << "[TGD DEBUG] Sat " << sat.toString() << " not found in satXvtTransTime, skip" << endl;
            continue;
        }
        Xvt satXvt = satXvtTransTime[sat];
        double corr = 0.0;

        // GPS TGD correction
        if (sat.system == "G")
        {
            double TGD = satXvt.typeTGDData["TGD"];
            corr = TGD * C_MPS;
            cout << "[TGD DEBUG] GPS Sat " << sat.toString() << " TGD=" << TGD << "s, corr=" << corr << "m" << endl;
            if (stv.second.count("C1")) stv.second["C1"] -= corr;
            if (stv.second.count("C2")) stv.second["C2"] -= corr;
        }
        // BDS TGD1/TGD2 correction
        else if (sat.system == "C")
        {
            double TGD1 = satXvt.typeTGDData["TGD1"];
            double TGD2 = satXvt.typeTGDData["TGD2"];
            double corr1 = TGD1 * C_MPS;
            double corr2 = TGD2 * C_MPS;

            cout << "[TGD DEBUG] Sat " << sat.toString()
                 << " TGD1=" << TGD1 << "s, corr1=" << corr1 << "m | "
                 << "TGD2=" << TGD2 << "s, corr2=" << corr2 << "m" << endl;

            if (stv.second.count("C2")) {
                double rawC2 = stv.second["C2"];
                stv.second["C2"] -= corr1;
                cout << "[TGD DEBUG] C2 raw obs:" << rawC2 << " -> corrected:" << stv.second["C2"] << endl;
            }
            if (stv.second.count("C7")) {
                double rawC7 = stv.second["C7"];
                stv.second["C7"] -= corr2;
                cout << "[TGD DEBUG] C7 raw obs:" << rawC7 << " -> corrected:" << stv.second["C7"] << endl;
            }
        }
    }
}

void SPPIFCode::convertObsType(ObsData &obsData) {

    SatTypeValueMap stvData;
    for (auto sd: obsData.satTypeValueData) {
        TypeValueMap tvData;
        for (auto td: sd.second) {
            tvData[td.first.substr(0, 2)] = td.second;
        }
        stvData[sd.first] = tvData;
    }

    // 替代
    obsData.satTypeValueData = stvData;
};

void SPPIFCode::processObs(ObsData &obsData) {
    SatIDSet satRejectedSet;

    // 根据解算模式分支处理观测
    switch (solveMode)
    {
        // 分支2：双频非组合，保留原始两个频点，不计算IF组合
        case DUAL_RAW:
        {
            for (auto &stv: obsData.satTypeValueData)
            {
                string sys = stv.first.system;
                std::pair<string, string> ifPair;
                try {
                    ifPair = ifCodeTypes.at(sys);
                }
                catch (...) {
                    satRejectedSet.insert(stv.first);
                    continue;
                }
                // 仅校验双频观测是否存在，不构造IF，原始C1/C2、C2/C7直接保留
                try{
                    stv.second.at(ifPair.first);
                    stv.second.at(ifPair.second);
                }catch(...){
                    satRejectedSet.insert(stv.first);
                }
            }
            break;
        }

        // 分支3：原有IF无电离层组合逻辑，完全不动
        case DUAL_IF_COMB:
        default:
        {
            // Loop through all the satellites
            for (auto &stv: obsData.satTypeValueData) {
                string sys = stv.first.system;
                // get type for current system
                std::pair<string, string> ifPair;
                try {
                    ifPair = ifCodeTypes.at(sys);
                }
                catch (...) {
                    satRejectedSet.insert(stv.first);
                }

                // if组合的具体公式为：
                // if12 = (f1^2*P1 - f2^2*P2)/(f1^2-f2^2);
                cout << "computeIF:" << "sys:"
                << sys << "type1:"
                << ifPair.first
                << "type2:" << ifPair.second << endl;

                double f1 = getFreq(sys, ifPair.first);
                double f2 = getFreq(sys, ifPair.second);
                if(debug)
                {
                    cout << "f1:" << f1 << "f2" << f2 << endl;
                }

                // 提取观测值
                double value1, value2, ifValue;
                try {
                    value1 = stv.second.at(ifPair.first);
                    value2 = stv.second.at(ifPair.second);
                    ifValue = (f1 * f1 * value1 - f2 * f2 * value2) / (f1 * f1 - f2 * f2);

                    if (debug) {
                        cout << "value1:" << value1 << endl;
                        cout << "value2:" << value2 << endl;
                        cout << "ifValue:" << ifValue << endl;
                    }
                    string ifCodeStr = "CC" + ifPair.first.substr(1, 1) + ifPair.second.substr(1, 1);
                    stv.second[ifCodeStr] = ifValue;
                }
                catch (...) {
                    satRejectedSet.insert(stv.first);
                }
            }
            break;
        }
    }

    // 统一移除所有不合格卫星（三种模式共用）
    for (auto sat: satRejectedSet) {
        obsData.satTypeValueData.erase(sat);
    }
};

std::map<SatID, Xvt> SPPIFCode::earthRotation(Eigen::Vector3d &xyz,
                                              std::map<SatID, Xvt> &satXvtTransTime) {

    std::map<SatID, Xvt> satXvtRecTime;
    for(auto stv: satXvtTransTime) {
        SatID sat = stv.first;
        XYZ xyzSat(stv.second.x);
        double dt = (xyzSat - xyz).norm() / C_MPS;

        double wt(0.0);
        wt = OMEGA_EARTH * dt;

        // todo:
        // Eigen中Vector3d是不是支持坐标旋转？
        // 请查询并修改

        double xSat, ySat, zSat;
        xSat = stv.second.x[0];
        ySat = stv.second.x[1];
        zSat = stv.second.x[2];

        double xSatRot(0.0), ySatRot(0.0);
        xSatRot = +std::cos(wt) * xSat + std::sin(wt) * ySat;
        ySatRot = -std::sin(wt) * xSat + std::cos(wt) * ySat;

        XYZ xyzRecTime;
        xyzRecTime[0] = xSatRot;
        xyzRecTime[1] = ySatRot;
        xyzRecTime[2] = zSat; // z轴不变

        double vxSat, vySat, vzSat;
        vxSat = stv.second.v[0];
        vySat = stv.second.v[1];
        vzSat = stv.second.v[2];

        double vxSatRot(0.0), vySatRot(0.0);
        vxSatRot = +std::cos(wt) * vxSat + std::sin(wt) * vySat;
        vySatRot = -std::sin(wt) * vxSat + std::cos(wt) * vySat;

        XYZ velRecTime;
        velRecTime[0] = vxSatRot;
        velRecTime[1] = vySatRot;
        velRecTime[2] = vzSat; // 不变

        // 替换位置和速度，得到旋转后的卫星产品
        Xvt xvtRecTime = stv.second;
        xvtRecTime.x = xyzRecTime;
        xvtRecTime.v = velRecTime;

        satXvtRecTime[sat] = xvtRecTime;
    };

    return satXvtRecTime;
};

void SPPIFCode::computeElevAzim(Eigen::Vector3d& xyz,
                                std::map<SatID,Xvt> & satXvt,
                                SatValueMap& tempElevData,
                                SatValueMap& tempAzimData
                                 )
{

    for(auto sx: satXvt)
    {
        SatID sat = sx.first;

        XYZ satXYZ = sx.second.x;

        // elevation
        double elev(0.0);
        double azim(0.0);
        elev = elevation(xyz, satXYZ);
        azim = azimuth(xyz, satXYZ);


        tempElevData[sat] = elev;
        tempAzimData[sat] = azim;
    }
};

void SPPIFCode::computeTropDelay(Eigen::Vector3d& xyz,
                                  std::map<SatID,Xvt>& satXvtRecTime,
                                  SatValueMap& satElevData)
{
    const double PI = 3.141592653589793;
    // 清空全局存储容器
    satTropData.clear();

    // 1. XYZ迭代转换BLH大地坐标（WGS84椭球）
    double lat, lon, hgt;
    double x = xyz[0], y = xyz[1], z = xyz[2];
    double a = 6378137.0;
    double f = 1.0 / 298.257223563;
    double e2 = 2 * f - f * f;

    lon = atan2(y, x);
    double p = sqrt(x * x + y * y);
    lat = atan2(z, p * (1 - e2));
    double N;
    for (int i = 0; i < 5; i++)
    {
        N = a / sqrt(1.0 - e2 * sin(lat) * sin(lat));
        hgt = p / cos(lat) - N;
        lat = atan2(z, p * (1.0 - e2 * N / (N + hgt)));
    }

    // 2. 标准气象参数（随测站高程修正气压）
    double lat_deg  = lat * 180.0 / PI;
    double hgt_km   = hgt / 1000.0;
    double P = 1013.25 * pow(1.0 - 2.26e-5 * hgt, 5.225);
    double T = 15.0 - 0.0065 * hgt + 273.15;
    double RH = 0.5;
    double e = 0.000061078 * RH * exp(17.27 * (T-273.15) / (T-35.85));

    // 3. Saastamoinen天顶干、湿延迟
    double ZHD = 0.0022768 * P / (1.0 - 0.00266 * cos(2*lat) - 0.00028 * hgt_km);
    double ZWD = 0.002277 * (1255.0 / T + 0.05) * e;

    // 4. Niell干映射纬度分段系数
    double ahd[3] = { 2.53e-3, 5.49e-3, 1.14e-3 };
    double bh[3]  = { 2.91e-3, 1.62e-3, 8.3e-4  };
    double ch[3]  = { 54.0,    53.0,   53.0    };

    int lat_idx;
    if     (lat_deg < 15.0) lat_idx = 0;
    else if(lat_deg < 30.0) lat_idx = 1;
    else                    lat_idx = 2;

    double ah_val = ahd[lat_idx];
    double bh_val = bh[lat_idx];
    double ch_val = ch[lat_idx];

    // 5. 遍历所有卫星计算斜对流延迟，存入类成员satTropData
    for(auto& entry : satElevData)
    {
        SatID sat = entry.first;
        double elevDeg = entry.second;
        double elev = elevDeg * PI / 180.0;
        double sinE = sin(elev);

        // Niell干分量映射函数
        double mh = (1.0 + ah_val/(1.0+bh_val/(1.0+ch_val/sinE)))
                  / (sinE + ah_val/(sinE+bh_val/(sinE+ch_val)));
        // 简化湿映射
        double mw = 1.0 / (sinE + 0.00095);

        double slant_zhd = ZHD * mh;
        double slant_zwd = ZWD * mw;
        double totalTrop = slant_zhd + slant_zwd;

        // 存入类全局SatValueMap satTropData
        satTropData[sat] = totalTrop;
    }
}

std::map<SatID, double> SPPIFCode::computeKlobucharIono(
    Eigen::Vector3d& recXYZ,
    CommonTime epoch,
    SatValueMap& satElevData,
    SatValueMap& satAzimData)
{
    const double PI = 3.141592653589793;

    // 1. 测站坐标 XYZ → 经纬度 (rad)
    double lat, lon, hgt;
    double x = recXYZ[0], y = recXYZ[1], z = recXYZ[2];
    double a = 6378137.0;
    double f = 1.0 / 298.257223563;
    double e2 = 2 * f - f * f;

    lon = atan2(y, x);
    double p = sqrt(x * x + y * y);
    lat = atan2(z, p * (1 - e2));
    double N;
    for (int i = 0; i < 5; i++) {
        N = a / sqrt(1 - e2 * sin(lat) * sin(lat));
        hgt = p / cos(lat) - N;
        lat = atan2(z, p * (1 - e2 * N / (N + hgt)));
    }

    std::map<SatID, double> ionoMap;

    // 2. 遍历所有卫星计算电离层延迟
    for (auto& entry : satElevData) {
        SatID sat = entry.first;
        double elevDeg = entry.second;
        double azimDeg = satAzimData[sat];
        string sys = sat.system;

        // GPS/BDS电离层高度区分
        double h_ion;
        if (sys == "G") h_ion = 350000.0;    // GPS 350km
        else if (sys == "C") h_ion = 375000.0; // BDS 375km
        else h_ion = 350000.0;

        // 读取广播星历电离层参数（改用类内pEphStore）
        double alpha[4] = {0}, beta[4] = {0};
        if (sys == "G") {
            alpha[0] = pEphStore->ionoCorrData["GPSA"][0];
            alpha[1] = pEphStore->ionoCorrData["GPSA"][1];
            alpha[2] = pEphStore->ionoCorrData["GPSA"][2];
            alpha[3] = pEphStore->ionoCorrData["GPSA"][3];
            beta[0]  = pEphStore->ionoCorrData["GPSB"][0];
            beta[1]  = pEphStore->ionoCorrData["GPSB"][1];
            beta[2]  = pEphStore->ionoCorrData["GPSB"][2];
            beta[3]  = pEphStore->ionoCorrData["GPSB"][3];
        }
        else if (sys == "C") {
            alpha[0] = pEphStore->ionoCorrData["BDSA"][0];
            alpha[1] = pEphStore->ionoCorrData["BDSA"][1];
            alpha[2] = pEphStore->ionoCorrData["BDSA"][2];
            alpha[3] = pEphStore->ionoCorrData["BDSA"][3];
            beta[0]  = pEphStore->ionoCorrData["BDSB"][0];
            beta[1]  = pEphStore->ionoCorrData["BDSB"][1];
            beta[2]  = pEphStore->ionoCorrData["BDSB"][2];
            beta[3]  = pEphStore->ionoCorrData["BDSB"][3];
        }

        double elev = elevDeg * PI / 180.0;
        double azim = azimDeg * PI / 180.0;
        YDSTime ydst = CommonTime2YDSTime(epoch);
        double tow = ydst.sod;

        // Klobuchar标准公式
        double RE = 6378137.0;
        double psi = asin( RE * cos(elev) / (RE + h_ion) );
        psi = (PI/2.0) - elev - psi;

        double phiI = lat + psi * cos(azim);
        if (phiI >  0.415) phiI = 0.415;
        if (phiI < -0.415) phiI = -0.415;

        double lambdaI = lon + psi * sin(azim) / cos(phiI);
        double phiM = phiI + 0.064 * cos(lambdaI - 1.617);

        double t = lambdaI / PI * 43200.0 + tow;
        t = fmod(t, 86400.0);
        if (t < 0) t += 86400.0;

        double amp = alpha[0] + phiM * (alpha[1] + phiM * (alpha[2] + phiM * alpha[3]));
        double per = beta[0] + phiM * (beta[1] + phiM * (beta[2] + phiM * beta[3]));
        if (amp < 0) amp = 0;
        if (per < 72000) per = 72000;

        double xVal = 2 * PI * (t - 50400.0) / per;
        double iono = 5.0e-9;
        if (fabs(xVal) < 1.57)
            iono += amp * cos(xVal);

        // 高度角映射函数
        double f = 1.0 / sqrt(1.0 - pow( (RE * cos(elev))/(RE + h_ion), 2 ));
        ionoMap[sat] = iono * f * C_MPS;

        cout << "Sat:" << sat << " [iono] delay(m):" << ionoMap[sat] << endl;
    }

    return ionoMap;
}

EquSys SPPIFCode::linearize(Eigen::Vector3d& xyz,
                                  std::map<SatID,Xvt>& satXvtRecTime,
                                  SatValueMap& satElevData,
                                  ObsData &obsData) {
    EquSys equSysTemp;
    equSysTemp.station = obsData.station;
    VariableSet varSetTemp;

    // 全局坐标未知数（三种模式都需要）
    Variable dx(obsData.station, Parameter::dX);
    Variable dy(obsData.station, Parameter::dY);
    Variable dz(obsData.station, Parameter::dZ);

    switch (solveMode)
    {

        // ====================== 模式2：双频非组合 ======================
        case DUAL_RAW:
        {
            for (auto &stv : obsData.satTypeValueData)
            {
                SatID sat = stv.first;
                double elev = satElevData.at(sat);
                if (elev < cutOffElev) continue;

                XYZ satXYZ = satXvtRecTime[sat].x;
                double rho = (satXYZ - xyz).norm();
                double clkBias = satXvtRecTime.at(sat).clkbias * C_MPS;
                double relCorr = satXvtRecTime.at(sat).relcorr * C_MPS;
                Eigen::Vector3d cosines;
                cosines[0] = (xyz.x() - satXYZ[0]) / rho;
                cosines[1] = (xyz.y() - satXYZ[1]) / rho;
                cosines[2] = (xyz.z() - satXYZ[2]) / rho;

                // 获取该系统双频码
                std::pair<string, string> freqPair = ifCodeTypes[sat.system];
                string c1 = freqPair.first;
                string c2 = freqPair.second;
                if (!stv.second.count(c1) || !stv.second.count(c2)) continue;

                double slantTrop = 0.0;
                if (enableTrop && satTropData.count(sat))
                    slantTrop = satTropData[sat];

                // 两条观测方程，分别生成
                for (string code : {c1, c2})
                {
                    double P = stv.second[code];
                    double computedObs = rho - clkBias - relCorr + slantTrop;
                    double prefit = P - computedObs;
                    EquID equID(sat, code);

                    equSysTemp.obsEquData[equID].prefit = prefit;
                    equSysTemp.obsEquData[equID].varCoeffData[dx] = cosines[0];
                    equSysTemp.obsEquData[equID].varCoeffData[dy] = cosines[1];
                    equSysTemp.obsEquData[equID].varCoeffData[dz] = cosines[2];

                    Variable cdtGPS(obsData.station, Parameter::cdt);
                    Variable cdtBDS(obsData.station, Parameter::cdtBDS);
                    if (sat.system == "G")
                    {
                        equSysTemp.obsEquData[equID].varCoeffData[cdtGPS] = 1.0;
                        varSetTemp.insert(cdtGPS);
                    }
                    else
                    {
                        equSysTemp.obsEquData[equID].varCoeffData[cdtBDS] = 1.0;
                        varSetTemp.insert(cdtBDS);
                    }

                    // 加权
                    double elevRad = elev * DEG_TO_RAD;
                    double weight = 1.0 / (sigIFCode * sigIFCode);
                    if (elev < 30) weight *= std::pow(std::sin(elevRad), 2);
                    equSysTemp.obsEquData[equID].weight = weight;

                    varSetTemp.insert(dx);
                    varSetTemp.insert(dy);
                    varSetTemp.insert(dz);
                }
            }
            break;
        }

        // ====================== 模式3：IF无电离层组合（原有逻辑完全不动） ======================
        case DUAL_IF_COMB:
        default:
        {
            for (auto stv: obsData.satTypeValueData) {
                SatID sat = stv.first;
                double elev = satElevData.at(sat);
                if(elev < cutOffElev) continue;

                XYZ satXYZ = satXvtRecTime[sat].x;
                double rho = ( satXYZ - xyz).norm();
                double clkBias = satXvtRecTime.at(sat).clkbias * C_MPS;
                double relCorr = satXvtRecTime.at(sat).relcorr * C_MPS;

                Eigen::Vector3d cosines;
                cosines[0] = (xyz.x() - satXYZ[0]) / rho;
                cosines[1] = (xyz.y() - satXYZ[1]) / rho;
                cosines[2] = (xyz.z() - satXYZ[2]) / rho;

                // 只识别IF组合码 CC12 / CC27
                for (auto tv: stv.second)
                {
                    bool isGpsIF = (sat.system == "G" && tv.first == "CC12");
                    bool isBdsIF = (sat.system == "C" && tv.first == "CC27");
                    if (!isGpsIF && !isBdsIF)
                        continue;

                    Variable cdtGPS(obsData.station, Parameter::cdt);
                    Variable cdtBDS(obsData.station, Parameter::cdtBDS);
                    EquID equID = EquID(sat, tv.first);
                    double slantTrop = 0.0;
                    if (enableTrop && satTropData.count(sat))
                    {
                        slantTrop = satTropData[sat];
                    }

                    double computedObs = (rho - clkBias - relCorr + slantTrop) ;
                    double prefit = tv.second - computedObs;

                    equSysTemp.obsEquData[equID].prefit = prefit;
                    equSysTemp.obsEquData[equID].varCoeffData[dx] = cosines[0];
                    equSysTemp.obsEquData[equID].varCoeffData[dy] = cosines[1];
                    equSysTemp.obsEquData[equID].varCoeffData[dz] = cosines[2];

                    if(isGpsIF)
                    {
                        equSysTemp.obsEquData[equID].varCoeffData[cdtGPS] = 1.0;
                        varSetTemp.insert(cdtGPS);
                    }
                    else if(isBdsIF)
                    {
                        equSysTemp.obsEquData[equID].varCoeffData[cdtBDS] = 1.0;
                        varSetTemp.insert(cdtBDS);
                    }

                    double elevRad = elev*DEG_TO_RAD;
                    double weight;
                    if(elev >= 30){
                        weight = 1.0 / (sigIFCode * sigIFCode);
                    }
                    else
                    {
                        weight = 1.0 / (sigIFCode * sigIFCode) * std::pow(std::sin(elevRad), 2);
                    }
                    equSysTemp.obsEquData[equID].weight = weight;

                    varSetTemp.insert(dx);
                    varSetTemp.insert(dy);
                    varSetTemp.insert(dz);
                }
            }
            break;
        }
    }

    equSysTemp.varSet = varSetTemp;
    return equSysTemp;
};

bool SPPIFCode::runPosVel(ObsData& obsData, XYZ& posOut, Eigen::Vector3d& velXYZ, double& clkDot)
{
    try
    {
        solve(obsData);
        posOut = xyz;
    }
    catch (...)
    {
        std::cout << "[SPP Err] Position solve fail, skip velocity" << std::endl;
        return false;
    }
    // 复用定位计算好的卫星XVT、高度角直接测速
    return velSolver.solveVelocityEpoch(xyz, obsData, satXvtRecTime, satElevData, velXYZ, clkDot);
}