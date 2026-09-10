#ifndef GNSSLAB_SPPIFCODE_H
#define GNSSLAB_SPPIFCODE_H

#include "GnssStruct.h"
#include "SolverLSQ.h"
#include "SPPVelocity.h"
#include "RinexNavStore.hpp"
#include <Eigen/Eigen>

class SPPIFCode {
public:

    enum SolveMode
    {
        DUAL_RAW,       // 双频非组合
        DUAL_IF_COMB    // 原有IF组合
    };

    SPPIFCode()
    : pEphStore(NULL), isRover(true), sigIFCode(1.0), cutOffElev(10),
    enableTrop(true), enableBDSTGD(true),solveMode(DUAL_IF_COMB)
    {}

    void setStationAsBase()
    {
        isRover = false;
    }

    void setRinexNavStore(RinexNavStore* pStore)
    {
        pEphStore = pStore;
    };

    void setIFCodeTypes(std::map<string, std::pair<string, string>>& ifTypes)
    {
        ifCodeTypes = ifTypes;
    };

    void setTropEnable(bool flag) { enableTrop = flag; }
    void setBDSTGDEnable(bool flag) { enableBDSTGD = flag; }
    // 新增设置解算模式接口
    void setSolveMode(SolveMode mode) { solveMode = mode; }

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

    void correctTGD(ObsData &obsData, std::map<SatID,Xvt>& satXvtTransTime);
    void convertObsType(ObsData &obsData);
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

    Result getResult();

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

    RinexNavStore* pEphStore;

    // 测速解算实例
    SPPVelocity velSolver;

    std::map<string, std::pair<string, string>> ifCodeTypes;

    SatID datumSat;

};


#endif //GNSSLAB_SPPIFCODE_H
