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
#include "SPPUCCodePhase.h"
#include "StringUtils.h"

// 诊断默认关闭。这些打印在逐历元、逐观测值的循环里；零基线那套数据是 1 Hz、
// 两个小时约 8000 个历元，打开后输出会淹没一切。只影响 stdout，不参与任何
// 数值计算，所以开关不可能移动回归基线。按 GnssFunc.cpp 的既有做法，
// 用 -DGNSSLAB_DEBUG_RTK=1 按目标打开。
#ifndef GNSSLAB_DEBUG_RTK
#define GNSSLAB_DEBUG_RTK 0
#endif

// CoordConvert.h 已经给 debug 提供了默认值（它自己的内联函数要用），
// 必须先撤销再重定义，否则是宏重定义。
#undef debug
#define debug GNSSLAB_DEBUG_RTK

#define SIG_UC_CODE 0.3
#define SIG_UC_PHASE 0.003

void SPPUCCodePhase::solve(ObsData &obsData) {
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


    // todo
    // 探讨双频非差观测值单点定位时卫星数条件？
    checkDualCodeTypes(obsData);

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

    //----------------------
    // 得到卫星发射时刻位置和钟差、相对论和TGD后，改正观测值延迟，并更新C1/C2等观测值
    //----------------------
    // todo:
    // correctTGD(obsData);

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
            if(debug)
                cout << "computeElevAzim" << endl;

            computeElevAzim(xyz, satXvtRecTime,satElevData,satAzimData);

            if(debug)
            {
                cout << "satElevData:" << endl;
                cout << satElevData << endl;
            }

            // todo:
            // computeIonoDelay();
            // computeTropDealy();
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

        cout
        << "iteration:"<< iter
        << "dxyz:"<< dxyz.transpose()
        << "xyz:" << xyz.transpose() << endl;


        // convergence threshold
        if (dxyz.norm() < 0.1) {
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

void SPPUCCodePhase::checkDualCodeTypes(ObsData &obsData)  {

    SatIDSet satRejectedSet;
    // Loop through all the satellites
    for (auto &stv: obsData.satTypeValueData) {
        string sys = stv.first.system;
        // get type for current system
        auto sysIt = dualCodeTypes.find(sys);
        if (sysIt == dualCodeTypes.end()) {
            satRejectedSet.insert(stv.first);
            continue;
        }

        // 双频非组合观测值，两个频率必须同时存在，否则方程将秩亏
        // A satellite survives if the system's list offers ANY pair it can
        // satisfy in full - not necessarily the first one. See the note on
        // dualCodeTypes in the header for why the list has more than one entry.
        bool complete = false;
        for (const auto &codePair : sysIt->second) {
            if (stv.second.count(codePair.first) && stv.second.count(codePair.second)) {
                complete = true;
                break;
            }
        }
        if (!complete) {
            satRejectedSet.insert(stv.first);
        }
    }
    // remove bad sat;
    for (auto sat: satRejectedSet) {
        obsData.satTypeValueData.erase(sat);
    }
};

EquSys SPPUCCodePhase::linearize(Eigen::Vector3d& xyz,
                                 std::map<SatID,Xvt>& satXvtRecTime,
                                 SatValueMap& satElevData,
                                 ObsData &obsData) {
    EquSys equSys;
    // The station has to be recorded here, not only on the variables.
    // SPPIFCode::linearize() sets it and this one did not, and the one reader in
    // the codebase is differenceStation()'s flag-merging overload, which uses it
    // to put the station back onto the merged cycle-slip keys. Left empty, every
    // key it produced carried an empty station and matched no ambiguity variable
    // at all - so the Kalman filter would never have seen a cycle slip. Nothing
    // numeric reads this field, which is why the omission was invisible.
    equSys.station = obsData.station;
    VariableSet varSetTemp;
    for (auto stv: obsData.satTypeValueData) {
        SatID sat = stv.first;

        double elev = satElevData.at(sat);
        double elevRad = elev*DEG_TO_RAD;

        // 跳过这颗卫星，不形成观测方程和未知参数数据
        if(elev < cutOffElev)
        {
            continue;
        }

        // 这里卫星的位置，应该是地球自转以后的卫星位置
        XYZ satXYZ;
        satXYZ = satXvtRecTime[sat].x;

        // rho
        double rho(0.0);
        rho = ( satXYZ - xyz).norm();

        if (debug) {
            cout << "rcvPos:" << xyz << endl;
            cout << "satXYZ:" << satXYZ << endl;
            cout << "rho:" << rho << endl;
        }

        double clkBias = satXvtRecTime.at(sat).clkbias * C_MPS;
        double relCorr = satXvtRecTime.at(sat).relcorr * C_MPS;

        double slantTrop(0.0);
        // to do
        // extract slant trop

        if(debug)
        {
            cout << "clkBias:" << clkBias << endl;
            cout << "relCorr:" << relCorr << endl;
            cout << "slantTrop" << slantTrop << endl;
        }

        // partials
        Eigen::Vector3d cosines;
        cosines[0] = (xyz.x() - satXYZ[0]) / rho;
        cosines[1] = (xyz.y() - satXYZ[1]) / rho;
        cosines[2] = (xyz.z() - satXYZ[2]) / rho;

        // todo
        // 请补充rhoDot，用于后续的单点测速

        // 首先定义所有可能的未知参数
        Variable dx(obsData.station, Parameter::dX);
        Variable dy(obsData.station, Parameter::dY);
        Variable dz(obsData.station, Parameter::dZ);
        // 接收机钟差按系统分开：GPS 用 cdt，北斗用 cdtBDS（见 GnssStruct.h 的
        // Parameter 枚举）。单系统解算时与原先写死的 Parameter::cdt 完全等价，
        // 只有 GPS+BDS 混合解时才产生差别。
        Variable cdt(obsData.station,
                     sat.system == "C" ? Parameter::cdtBDS : Parameter::cdt);
        // 把当前观测方程未知参数插入到总体的未知参数
        varSetTemp.insert(dx);
        varSetTemp.insert(dy);
        varSetTemp.insert(dz);
        varSetTemp.insert(cdt);

        // 北斗二号与三号之间的接收机系统差。两代的信号走不同的接收机通道，
        // 卫星钟基准也不同，所以这个偏差在跨代的双差里不会抵消。
        //
        // 只在 BDS-3 卫星的方程上加它、系数取 1；BDS-2 的方程上系数为 0。
        // 于是同一个历元内：同代之间双差系数相减为 0（不影响），跨代之间剩
        // ±1（可估）。真正决定它有没有被消掉的是 differenceSat() 的系数差分，
        // 那一步必须把它当作坐标那样做差，而不是像模糊度那样原样带过去。
        //
        // **只加在伪距方程上，不加在载波相位方程上。** 这不是省事，是必须：
        // 相位上的系统差与模糊度完全不可分——把一个共享的常数加到全部三代
        // 卫星的相位方程上，再让每颗星的模糊度各自减去同样的量，方程一个字
        // 都不变。所以那个参数根本没有可估性，硬加进去只会让它和模糊度强相关。
        // 实测（零基线全量 7934 历元）：模糊度差分的 ratio 中位数因此从 1453
        // 掉到 6.5，固定率从 96.7% 掉到 91.6%。只加在伪距上，两者都立刻恢复。
        // 这与"相位硬件延迟并入模糊度"这个通行约定是同一件事。
        Variable ifb(obsData.station, Parameter::ifb);
        const bool useIfb = estimateISB && (bdsGeneration(sat) == 3);
        if (useIfb)
        {
            varSetTemp.insert(ifb);
        }

        // 没在 dualCodeTypes 里配置的系统不形成观测方程。
        // checkDualCodeTypes() 在 solve() 里已经剔除过一轮，这里再挡一次是为了
        // 直接调用 linearize() 时也不至于让下面的 .at() 抛异常。
        if (dualCodeTypes.find(sat.system) == dualCodeTypes.end())
        {
            continue;
        }

        // 两个码类型由 dualCodeTypes 给出，相位类型就是同频段的 L 类型
        // （C1->L1、C2->L2、C7->L7）。与 RinexObsReader/convertObsType 把观测
        // 类型截断成两位的规则一致。于是同一段代码同时支持 GPS 的 C1/C2 和
        // 北斗的 C2/C7（即 B1I/B2I），不必为每个系统复制一份。
        //
        // WHICH pair is per-satellite when the system lists more than one: the
        // first it can satisfy in full. checkDualCodeTypes() has already
        // guaranteed that at least one exists; the fallback below only guards a
        // direct call to linearize().
        const std::vector<std::pair<string, string>> &pairs = dualCodeTypes.at(sat.system);
        string code1, code2;
        bool pairFound = false;
        for (const auto &codePair : pairs) {
            if (stv.second.count(codePair.first) && stv.second.count(codePair.second)) {
                code1 = codePair.first;
                code2 = codePair.second;
                pairFound = true;
                break;
            }
        }
        if (!pairFound) {
            continue;
        }
        const string phase1 = "L" + code1.substr(1);
        const string phase2 = "L" + code2.substr(1);

        // 对每个观测值，都需要存储对应的未知参数及其偏导数
        for (auto tv: stv.second)
        {
            // 这里原先写的是 if(sat.system=="G")。系统现在由 dualCodeTypes 决定，
            // 未配置的系统已在上面 continue 掉，所以只剩下块作用域。
            {
                // 电离层所有频率估计的都是第一频率的伪距的电离层延迟
                Variable ionoC1G(obsData.station,
                                 sat,
                                 Parameter::iono,
                                 ObsID(sat.system, code1));

                // 把ionoC1G插入到观测方程
                varSetTemp.insert(ionoC1G);

                double gamma = getGamma(sat.system, code1, code2);

                if (tv.first == code1 )
                {
                    EquID equID = EquID(sat, tv.first);

                    //>> 先验残差
                    double prefit;
                    double computedObs = (rho - clkBias - relCorr + slantTrop) ;
                    prefit = tv.second - computedObs;

                    if(debug)
                    {
                        cout << "sat:" << sat
                             << fixed << setprecision(3)
                             << "type:" << tv.first
                             << "obs:" << tv.second
                             << "rho:" << rho
                             << "clkBias:" << clkBias
                             << "relCorr:" << relCorr
                             << "prefit:" << prefit
                             << endl;
                    }

                    equSys.obsEquData[equID].prefit = prefit;
                    equSys.obsEquData[equID].varCoeffData[dx] = cosines[0];
                    equSys.obsEquData[equID].varCoeffData[dy] = cosines[1];
                    equSys.obsEquData[equID].varCoeffData[dz] = cosines[2];
                    equSys.obsEquData[equID].varCoeffData[cdt] = 1.0;
                    equSys.obsEquData[equID].varCoeffData[ionoC1G] = 1.0;
                    if (useIfb) equSys.obsEquData[equID].varCoeffData[ifb] = 1.0;

                    // Compute the weight according to elevation
                    double weight;
                    if(elev >= 30){
                        weight = 1.0 / (SIG_UC_CODE * SIG_UC_CODE);
                    }
                    else
                    {
                        weight = 1.0 / (SIG_UC_CODE * SIG_UC_CODE) * std::pow(std::sin(elevRad), 2);
                    }

                    equSys.obsEquData[equID].weight = weight; // IF组合方差为1.0m


                }
                else if ( tv.first == code2 )
                {
                    EquID equID = EquID(sat, tv.first);

                    //>> 先验残差
                    double prefit;
                    double computedObs = (rho - clkBias - relCorr + slantTrop) ;
                    prefit = tv.second - computedObs;

                    if(debug)
                    {
                        cout << "sat:" << sat
                             << fixed << setprecision(3)
                             << "type:" << tv.first
                             << "obs:" << tv.second
                             << "rho:" << rho
                             << "clkBias:" << clkBias
                             << "relCorr:" << relCorr
                             << "prefit:" << prefit
                             << endl;
                    }

                    equSys.obsEquData[equID].prefit = prefit;
                    equSys.obsEquData[equID].varCoeffData[dx] = cosines[0];
                    equSys.obsEquData[equID].varCoeffData[dy] = cosines[1];
                    equSys.obsEquData[equID].varCoeffData[dz] = cosines[2];
                    equSys.obsEquData[equID].varCoeffData[cdt] = 1.0;
                    equSys.obsEquData[equID].varCoeffData[ionoC1G] = gamma;
                    if (useIfb) equSys.obsEquData[equID].varCoeffData[ifb] = 1.0;

                    // Compute the weight according to elevation
                    double weight;
                    if(elev >= 30){
                        weight = 1.0 / (SIG_UC_CODE * SIG_UC_CODE);
                    }
                    else
                    {
                        weight = 1.0 / (SIG_UC_CODE * SIG_UC_CODE) * std::pow(std::sin(elevRad), 2);
                    }

                    equSys.obsEquData[equID].weight = weight; // IF组合方差为1.0m


                }
                else if (tv.first == phase1 )
                {
                    EquID equID = EquID(sat, tv.first);

                    //>> 先验残差
                    double prefit;
                    double computedObs = (rho - clkBias - relCorr + slantTrop) ;
                    prefit = tv.second - computedObs;

                    if(debug)
                    {
                        cout << "sat:" << sat
                             << fixed << setprecision(3)
                             << "type:" << tv.first
                             << "obs:" << tv.second
                             << "rho:" << rho
                             << "clkBias:" << clkBias
                             << "relCorr:" << relCorr
                             << "prefit:" << prefit
                             << endl;
                    }

                    //>>>> 定义未知系数变量和系数值
                    equSys.obsEquData[equID].prefit = prefit;
                    equSys.obsEquData[equID].varCoeffData[dx] = cosines[0];
                    equSys.obsEquData[equID].varCoeffData[dy] = cosines[1];
                    equSys.obsEquData[equID].varCoeffData[dz] = cosines[2];
                    equSys.obsEquData[equID].varCoeffData[cdt] = 1.0;
                    equSys.obsEquData[equID].varCoeffData[ionoC1G] = -1.0;

                    // 定义模糊度变量
                    Variable ambL1G(obsData.station,
                                     sat,
                                     Parameter::ambiguity,
                                     ObsID(sat.system, tv.first));

                    // 将模糊度变量存储到全体变量列表中
                    varSetTemp.insert(ambL1G);

                    double wavelength = getWavelength(sat.system, safeStoi(tv.first.substr(1,1)));
                    equSys.obsEquData[equID].varCoeffData[ambL1G] = wavelength;


                    // Compute the weight according to elevation
                    double weight;
                    if(elev >= 30){
                        weight = 1.0 / (SIG_UC_PHASE * SIG_UC_PHASE);
                    }
                    else
                    {
                        weight = 1.0 / (SIG_UC_PHASE * SIG_UC_PHASE) * std::pow(std::sin(elevRad), 2);
                    }
                    equSys.obsEquData[equID].weight = weight;


                }
                else if (tv.first == phase2 )
                {
                    EquID equID = EquID(sat, tv.first);

                    //>> 先验残差
                    double prefit;
                    double computedObs = (rho - clkBias - relCorr + slantTrop) ;
                    prefit = tv.second - computedObs;
                    equSys.obsEquData[equID].prefit = prefit;

                    if(debug)
                    {
                        cout << "sat:" << sat
                             << fixed << setprecision(3)
                             << "type:" << tv.first
                             << "obs:" << tv.second
                             << "rho:" << rho
                             << "clkBias:" << clkBias
                             << "relCorr:" << relCorr
                             << "prefit:" << prefit
                             << endl;
                    }

                    //>>>> 定义未知系数变量和系数值


                    equSys.obsEquData[equID].varCoeffData[dx] = cosines[0];
                    equSys.obsEquData[equID].varCoeffData[dy] = cosines[1];
                    equSys.obsEquData[equID].varCoeffData[dz] = cosines[2];
                    equSys.obsEquData[equID].varCoeffData[cdt] = 1.0;
                    equSys.obsEquData[equID].varCoeffData[ionoC1G] = -gamma;

                    // 定义模糊度变量
                    Variable ambL2G(obsData.station,
                                    sat,
                                    Parameter::ambiguity,
                                    ObsID(sat.system, tv.first));

                    // 将模糊度变量存储到全体变量列表中
                    varSetTemp.insert(ambL2G);

                    double wavelength = getWavelength(sat.system, safeStoi(tv.first.substr(1,1)));
                    equSys.obsEquData[equID].varCoeffData[ambL2G] = wavelength;

                    // Compute the weight according to elevation
                    double weight;
                    if(elev >= 30){
                        weight = 1.0 / (SIG_UC_PHASE * SIG_UC_PHASE);
                    }
                    else
                    {
                        weight = 1.0 / (SIG_UC_PHASE * SIG_UC_PHASE) * std::pow(std::sin(elevRad), 2);
                    }
                    equSys.obsEquData[equID].weight = weight;

                }
            }

        }
    }
    equSys.varSet = varSetTemp;

    return equSys;
};