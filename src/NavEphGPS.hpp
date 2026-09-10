#ifndef NavEphGPS_HPP
#define NavEphGPS_HPP

#include <string>
#include <cmath>

#include "TimeConvert.h"
#include "GnssStruct.h"

class NavEphGPS {
public:
    /// Default constuctor
    NavEphGPS(void)
            : beginValid(END_OF_TIME),
              endValid(BEGINNING_OF_TIME) {
        beginValid.m_timeSystem = TimeSystem::GPS;
        endValid.m_timeSystem = TimeSystem::GPS;
    }

    /// Destructor
    virtual ~NavEphGPS(void) {}

    /// Dump the overhead information to the given output stream.
    /// throw Invalid Request if the required data has not been stored.
    void printData() const;

    /// Compute the satellite clock bias (seconds) at the given time
    /// throw Invalid Request if the required data has not been stored.
    double svClockBias(const CommonTime &t) const;

    /// Compute the satellite clock drift (sec/sec) at the given time
    /// throw Invalid Request if the required data has not been stored.
    double svClockDrift(const CommonTime &t) const;

    /// Compute satellite relativity correction (sec) at the given time
    /// throw Invalid Request if the required data has not been stored.
    double svRelativity(const CommonTime &t) const;

    /// return URA of broadcast
    double svURA(const CommonTime &t) const;

    /// Compute satellite position at the given time.
    Xvt svXvt(const CommonTime &t) const;

    bool isValid(const CommonTime &ct) const;

    ///Ephemeris data
    ///   SV/EPOCH/SV CLK
    CivilTime CivilToc;
    double Toc;                ///< Time of clock (year/month/day/hour/min/sec GPS)
    double af0;                ///< SV clock bias(seconds)
    double af1;                ///< SV clock drift(sec/sec)
    double af2;                ///< SV clock drift rate (sec/sec2)

    ///   BROADCAST ORBIT-1
    double IODE;               ///< IODE Issue of Data, Ephemeris
    double Crs;                ///< (meters)
    double Delta_n;            ///< Mean Motion Difference From Computed Value(semi-circles/sec)
    double M0;                 ///< Mean Anomaly at Reference Time(semi-circles)

    ///   BROADCAST ORBIT-2
    double Cuc;                ///< (radians)
    double ecc;                ///< Eccentricity
    double Cus;                ///< (radians)
    double sqrt_A;             ///< Square Root of the Semi-Major Axis(sqrt(m))

    ///   BROADCAST ORBIT-3
    double Toe;                ///< Time of Ephemeris(sec of GPS week)
    double Cic;                ///< (radians)
    double OMEGA_0;            ///< Longitude of Ascending Node of Orbit Plane(semi-circles)
    double Cis;                ///< (radians)

    ///   BROADCAST ORBIT-4
    double i0;                 ///< Inclination Angle at Reference Time(semi-circles)
    double Crc;                ///< (meters)
    double omega;              ///< Argument of Perigee(semi-circles)
    double OMEGA_DOT;          ///< Rate of Right Ascension(semi-circles/sec)

    ///   BROADCAST ORBIT-5
    double IDOT;               ///< Rate of Inclination Angle(semi-circles/sec)
    double L2Codes;
    double GPSWeek;            ///< to go with TOE, Continuous number,
    ///not mod 1024
    double L2Pflag;

    ///   BROADCAST ORBIT-6
    double URA;                ///< SV accuracy(meters) See GPS ICD
    double SV_health;          ///< bits 17-22 w 3 sf 1
    double TGD;                ///< (seconds)
    double IODC;               ///< Issue of Data, Clock

    ///   BROADCAST ORBIT-7
    long HOWtime;              ///< Transmission time of message， sec of GPSWeek
    double fitInterval;        ///< Fit Interval in hours

    ///member data
    CommonTime ctToc;          ///< Toc in CommonTime form
    CommonTime ctToe;          ///< Toe in CommonTime form
    CommonTime transmitTime;   ///< Transmission time in CommonTime form
    CommonTime beginValid;     ///< Time at beginning of validity
    CommonTime endValid;       ///< Time at end of fit validity

private:
    /// Get the fit interval in hours from the fit interval flag and the IODC
    static short getFitInterval(const short IODC, const short fitIntFlag);

}; // end class NavEphGPS


#endif // NavEphGPS_HPP