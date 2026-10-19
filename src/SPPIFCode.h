#ifndef GNSSLAB_SPPIFCODE_H
#define GNSSLAB_SPPIFCODE_H

#include "GnssStruct.h"
#include "SolverLSQ.h"
#include "SPPVelocity.h"
#include "RinexNavStore.hpp"
#include "EphStore.h"
#include <Eigen/Eigen>

class SinexBiasStore;

class SPPIFCode {
public:

    enum SolveMode
    {
        DUAL_RAW,       // 双频非组合
        DUAL_IF_COMB    // 原有IF组合
    };

    SPPIFCode()
    : cutOffElev(10), isRover(true), sigIFCode(1.0),
    enableTrop(true), enableBDSTGD(true), solveMode(DUAL_IF_COMB),
    pEphStore(NULL), pBiasStore(NULL)
    {}

    void setStationAsBase()
    {
        isRover = false;
    }

    void setRinexNavStore(RinexNavStore* pStore)
    {
        pEphStore = pStore;
    };

    // Any orbit/clock source, not just the broadcast one. SP3Store already
    // implements EphStore (it carries a RINEX clock reader alongside the SP3
    // orbits), so passing one here runs the whole solver on precise products
    // without touching anything below this point.
    void setEphStore(EphStore* pStore)
    {
        pEphStore = pStore;
    };

    // Per-signal code-bias corrections (SINEX-BIAS OSB). Only meaningful with a
    // precise clock whose datum the bias product is referred to; a null store
    // leaves every observation exactly as read.
    void setBiasStore(SinexBiasStore* pStore)
    {
        pBiasStore = pStore;
    };

    void setIFCodeTypes(std::map<string, std::pair<string, string>>& ifTypes)
    {
        ifCodeTypes = ifTypes;
    };

    void setTropEnable(bool flag) { enableTrop = flag; }
    void setBDSTGDEnable(bool flag) { enableBDSTGD = flag; }
    // 新增设置解算模式接口
    void setSolveMode(SolveMode mode) { solveMode = mode; }

    // Elevation mask in degrees; satellites below it are excluded from the solution.
    void setCutOffElev(double elevDeg) { cutOffElev = elevDeg; }

    // Observation sigma (metres) for the ionosphere-free code combination.
    // The weight applied is 1/sigma^2, so this is a strong lever on the result:
    // moving it from 1.0 to 0.3 scales every weight by roughly 11x.
    void setSigIFCode(double sigma) { sigIFCode = sigma; }

    void solve(ObsData &obsData);

    std::map<SatID,Xvt> computeSatPos(ObsData &obsData);
    Xvt computeAtTransmitTime(const CommonTime& tr,
                              const double& pr,
                              const SatID& sat);

    void computeElevAzim(Eigen::Vector3d& xyz,
                          std::map<SatID,Xvt> & satXvtTransTime,
                          SatValueMap& tempElevData,
                          SatValueMap& tempAzimData);
    void computeTropDelay(Eigen::Vector3d& xyz,
                      std::map<SatID,Xvt>& satXvtRecTime,
                      SatValueMap& satElevData);

    void correctCodeBias(ObsData &obsData);
    void correctTGD(ObsData &obsData, std::map<SatID,Xvt>& satXvtTransTime);
    void convertObsType(ObsData &obsData);

    /**
     * Whether one raw code observable of a satellite needs an OSB correction.
     *
     * The base class says yes for every code observable. SPPUCCodePhase
     * overrides it to consider only the code pair the satellite will actually
     * use: an observable outside that pair never reaches an equation, so
     * counting it as "missing a bias" would inflate the manifest's counter with
     * observations that could not have been corrected or used.
     */
    virtual bool needsCodeBias(const SatID &sat,
                               const TypeValueMap &obs,
                               const std::string &obsType3) const;
    void processObs(ObsData &obsData);
    std::map<SatID,Xvt> earthRotation(Eigen::Vector3d& xyz,
                                      std::map<SatID,Xvt> & satXvtTransTime);
    // Klobuchar电离层模型，仅单频模式调用
    std::map<SatID, double> computeKlobucharIono(Eigen::Vector3d& recXYZ,
                                                 CommonTime epoch,
                                                 SatValueMap& satElevData,
                                                 SatValueMap& satAzimData);
    EquSys linearize(Eigen::Vector3d& xyz,
                     std::map<SatID,Xvt>& satXvtRecTime,
                     SatValueMap& satElevData,
                     ObsData& obsData);
    // 联合定位+测速入口
    bool runPosVel(ObsData& obsData, XYZ& posOut, Eigen::Vector3d& velXYZ, double& clkDot);
    // 获取测速对象
    SPPVelocity& getVelObj() { return velSolver; }

    SatID getDatumSat()
    {
        double maxElev(0.0);
        SatID datumSat;
        for(auto se: satElevData)
        {
            if(se.second>maxElev)
            {
                maxElev = se.second;
                datumSat = se.first;
            }
        }
        return datumSat;
    };

    SatValueMap getSatElevData()
    {
        return satElevData;
    }

    Vector3d getXYZ()
    {
        return xyz;
    }

    // The linearized observation equations last produced by linearize().
    //
    // Lives here rather than on SPPUCCodePhase because `equSys` is declared in
    // this class (below, under `protected`) and every other accessor for it -
    // getDatumSat(), getSatElevData(), getXYZ() - is already here. The RTK
    // driver in apps/rtk.cpp reaches for all four through the derived
    // object.
    const EquSys& getEquSys() const
    {
        return equSys;
    }

    Result getResult();

    // Diagnostics for the OSB path. Zero whenever no bias store is attached.
    long getCodeBiasCorrections() const { return codeBiasCorrections; }
    long getCodeBiasMissing() const { return codeBiasMissing; }

    ~SPPIFCode(){};

    // 继承类需要访问这个成员
protected:

    double cutOffElev;

    bool isRover;
    double sigIFCode;

    bool enableTrop;    // 对流层改正开关
    bool enableBDSTGD;  // BDS TGD改正开关
    SolveMode solveMode;// 新增：解算模式标记

    EquSys equSys;
    Result result;

    Vector3d xyz;
    Vector3d dxyz;

    std::map<SatID,Xvt> satXvtTransTime;
    std::map<SatID,Xvt> satXvtRecTime;

    SatValueMap  satElevData;
    SatValueMap  satAzimData;
    SatValueMap  satTropData;

    SolverLSQ  solverLsq;

    // The orbit/clock source in use: a RinexNavStore by default (the only kind
    // that exists below the app layer), an SP3Store when a run was handed
    // precise products. Typed as the interface so both fit.
    EphStore* pEphStore;

    // Optional OSB store. Null is the default and the byte-anchored path.
    SinexBiasStore* pBiasStore;

    // Diagnostics for the manifest: corrections applied and code observations
    // the bias file did not cover (left uncorrected).
    long codeBiasCorrections = 0;
    long codeBiasMissing = 0;

    // 测速解算实例
    SPPVelocity velSolver;

    std::map<string, std::pair<string, string>> ifCodeTypes;

    SatID datumSat;

};


#endif //GNSSLAB_SPPIFCODE_H
