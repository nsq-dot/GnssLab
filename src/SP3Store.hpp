
#ifndef SP3Store_INCLUDE
#define SP3Store_INCLUDE

#include <map>
#include <vector>
#include <algorithm>
#include <iostream>

#include "Exception.h"
#include "TimeStruct.h"
#include "CoordStruct.h"
#include "Rx3ClockReader.hpp"
#include "GnssStruct.h"
#include "EphStore.h"
#include <Eigen/Eigen>

using namespace Eigen;

struct PosVel {
    PosVel()
            : pos(0.0, 0.0, 0.0), vel(0.0, 0.0, 0.0) {};
    Vector3d pos;
    Vector3d vel;
};

struct ClockData {
    ClockData()
            : clockBias(0.0), clockDrift(0.0) {};

    double clockBias;
    double clockDrift;
};

class SP3Store : public EphStore {
public:

    // Default constructor
    SP3Store() {
        satSet.clear();
        satPositionData.clear();
        satClockData.clear();
    }

    // Destructor
    virtual ~SP3Store() {}

    virtual Xvt getXvt(const SatID &sat, const CommonTime &time)
    noexcept(false);

    // Whether getXvt adds the computed relativistic correction on top of the
    // clock it read. The IGS/CODE clock products do NOT contain that term -
    // they are the same convention as the broadcast polynomial, which the
    // caller adds relativity to separately. Measured against bds_eph: the
    // precise clock minus the broadcast polynomial is 1.9/10.3/-2.8 ns, i.e.
    // the broadcast clock's own error, not the ±7-25 ns the term is worth.
    // Leaving this true is therefore correct for a positioning solver too; a
    // run that turned it off (2025 Wuhan, 24 h) went from 1.06 m mean to
    // 6.32 m - the term is ±2-7.5 m per satellite.
    bool addRelativity = true;

    PosVel getPosVel(const SatID &sat, const CommonTime &time);

    ClockData getClock(const SatID &sat, const CommonTime &time);

    void loadSP3File(const std::string &filename) noexcept(false);

    void loadSP3Header(std::fstream &strm);

    void loadSP3Data(std::fstream &strm);

    SatIDSet getSatSet() {
        return satSet;
    }

    void loadRinexClockFile(const std::string &filename) noexcept(false);

    // member data
private:

    std::map<SatID, std::map<CommonTime, Vector3d>> satPositionData;

    // ClockSatStore for SP3 or RINEX clock data
    std::map<SatID, std::map<CommonTime, double>> satClockData;

    //>>> sp3 header

    char version;

    double epochInterval;
    int numberOfEpochs;        //< Number of epochs in this file
    std::string dataUsed;      //< Types of data input into the positions
    std::string coordSystem;   //< Coordinate System of the data
    std::string orbitType;     //< Type of Orbit Estimate
    std::string agency;        //< Agency generating the Orbit

    // the following are specific to version 'c'
    string system;        //< system of satellites in file, e.g. G for GPS
    TimeSystem timeSystem;  //< Time system used
    double basePV;          //< Base used in Pos or Vel (mm or 10**-4mm/sec)
    double baseClk;         //< Base used in Clk or rate (psec or 10**-4psec/sec)
    /// Map<SatID,accuracy flag> (all SVs in file)
    std::map<SatID, short> satList;
    /// vector of 4 comment lines
    std::vector<std::string> comments;

    std::set<SatID> satSet;

    //>>> sp3 data

    CommonTime currentEpoch;
    std::string lastLine;
    std::vector<std::string> warnings;

    // memebers
    char RecType;    // Data type indicator. P position, V velocity, * epoch
    SatID sat;       // Satellite ID
    CommonTime time; // Time of epoch for this record
    double x[3];     // The three-vector for position | velocity (m | dm/s).
    double clk;      // The clock bias or drift for P|V (microsec|1).

    /// the rest of the member are for version c only
    int sig[4];      // Four-vector of integer exponents for estimated sigma
    // of position,clock or velocity,clock rate; sigma = base**n
    // units are mm,psec or 10^-4 mm/sec,psec/sec); base in head.
    // n is >= 0, and n = -1 means unknown (blank in file)
    bool clockEventFlag;    // clock event flag, 'E' in file
    bool clockPredFlag;     // clock prediction flag, 'P' in file
    bool orbitManeuverFlag; // orbit maneuver flag, 'M' in file
    bool orbitPredFlag;     // orbit prediction flag, 'P' in file

    bool reachEOF;
    int numCol;

    /// data for optional P|V Correlation record
    bool correlationFlag;   // If true, on input: a correlation record was read;
    // on output: stream should output correlation.
    unsigned sdev[4];  // std dev of 3 positions (XYZ,mm) and clock (psec)
    // or velocities(10^-4 mm/sec) and clock rate (10^-4 ps/s)
    int correlation[6];// elements of correlation matrix: xy,xz,xc,yz,yc,zc

    Rx3ClockReader clockReader;


}; // end class SP3Store


#endif // SP3Store_INCLUDE
