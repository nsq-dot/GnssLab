#ifndef GNSSLAB_SPPVELOCITY_H
#define GNSSLAB_SPPVELOCITY_H

// 必须前置包含GnssStruct，所有方程/变量结构都在这里
#include "GnssStruct.h"
#include "SolverLSQ.h"
#include "RinexNavStore.hpp"
#include "CoordConvert.h"
#include <Eigen/Eigen>
#include <map>
#include <iostream>
#include <cmath>

#define VELOCITY_DEBUG 1

class SPPVelocity
{
public:
    SPPVelocity()
        : cutOffElev(10.0), sigDoppler(0.05)
    {}

    void setCutOffElev(double elev) { cutOffElev = elev; }
    void setDopplerSigma(double sig) { sigDoppler = sig; }

    bool solveVelocityEpoch(const Eigen::Vector3d& recXYZ,
                            ObsData& obsData,
                            std::map<SatID, Xvt>& satXvtRecTime,
                            SatValueMap& satElevData,
                            Eigen::Vector3d& velXYZOut,
                            double& clkDotOut);

    Eigen::Vector3d xyz2Enu(const Eigen::Vector3d& recXYZ, const Eigen::Vector3d& velXYZ);

    EquSys getVelEquSys() const { return velEquSys; }

private:
    void linearizeVelocity(const Eigen::Vector3d& recXYZ,
                           ObsData& obsData,
                           std::map<SatID, double>& dopMap,
                           std::map<SatID, double>& lambdaMap,
                           std::map<SatID, Xvt>& satXvtRecTime,
                           SatValueMap& satElevData);

    std::map<SatID, double> extractValidDoppler(ObsData& obsData,
                                                std::map<SatID, Xvt>& satXvtRecTime,
                                                SatValueMap& satElevData,
                                                std::map<SatID, double>& lambdaMap);

    int indexOfVar(const VariableSet& varSet, const Variable& v);

    bool solveLSQGetResult(Eigen::Vector3d& velXYZOut, double& clkDotOut);

private:
    double cutOffElev;
    double sigDoppler;
    EquSys velEquSys;
    SolverLSQ velSolver;
};

#endif //GNSSLAB_SPPVELOCITY_H
