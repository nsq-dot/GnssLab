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

#ifndef TimeStruct_H
#define TimeStruct_H

#include <sys/time.h>
#include "Exception.h"
#include <limits>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include "Const.h"

using namespace std;

class TimeSystem {
public:

    // list of time systems supported by this class
    enum SystemType {
        Unknown = 0,
        GPS, // GPS system time
        GLO, // glonass system time
        GAL, // Galileo system time
        BDT, // BDS system time
        QZS, // qzss time
        UTC, // utc
        TAI,
        TT,
        count // the number of systems
    };

    // Constructor, including empty constructor
    TimeSystem(const SystemType sys = GPS) {
        if (sys >= count)
            system = GPS;
        else
            system = sys;
    }

    // Constructor
    TimeSystem(const std::string str) {
        for (int i = 0; i < count; i++) {
            if (sys_strings[i] == str) {
                system = static_cast<SystemType>(i);
                break;
            }
        }
    }

    bool operator==(const TimeSystem &right) const {
        if (system == right.system)
            return true;
        else
            return false;
    }

    bool operator!=(const TimeSystem &right) const {
        return (!operator==(right));
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << sys_strings[static_cast<int>(system)];

        return oss.str();
    };

    // time system
    SystemType system;

    // string for enum sys, 1 vs 1
    static const std::string sys_strings[];

}; // end class TimeSystem



inline std::ostream &operator<<(std::ostream &os, TimeSystem &ts) {
    return os << ts.toString() << std::endl;
}

/**
 *  定义一个通用时间的目的是为了实现不同历法之间的快速转换；
 *  考虑到计算精度和计算方便，需要把时间分为day和second of day两个变量；
 *  这是因为，如果用一个second of day作为唯一变量，受到计算机存储字长的影响，
 *  在时间转换时，精度会下降，难以满足高精度需求。
 */
class CommonTime {
public:

    CommonTime() {
        m_day = 0;
        m_sod = 0.0;
        m_timeSystem = TimeSystem::GPS;
    }

    CommonTime(long day,
               double sod = 0.0,
               TimeSystem timeSystem = TimeSystem::GPS) {
        m_day = day;
        m_sod = sod;
        m_timeSystem = timeSystem;
    }

    CommonTime(const CommonTime &right)
            : m_day(right.m_day), m_sod(right.m_sod), m_timeSystem(right.m_timeSystem) {}

    // Destructor.
    virtual ~CommonTime() {}

    void set(
            long day,
            double sod = 0.0,
            TimeSystem timeSystem = TimeSystem::GPS
    ) {
        m_day = day;
        m_sod = sod;
        m_timeSystem = timeSystem;
    };

    void setTimeSystem(const TimeSystem& timeSystem)
    {
        m_timeSystem = timeSystem;
    }

    void get(
            long &day,
            double &sod,
            TimeSystem &timeSystem) const {
        day = m_day;
        sod = m_sod;
        timeSystem = m_timeSystem;
    };

    CommonTime &operator=(const CommonTime &right) {
        m_day = right.m_day;
        m_sod = right.m_sod;
        m_timeSystem = right.m_timeSystem;
        return *this;
    };

    double operator-(const CommonTime &right) const {
        if (m_timeSystem != right.m_timeSystem) {
            InvalidRequest ir("CommonTime objects not in same time system, cannot be differenced");
            throw (ir);
        }

        return (SEC_PER_DAY * static_cast<double>( m_day - right.m_day  ) +
                m_sod - right.m_sod);
    }

    CommonTime operator+(double sec) const {
        return CommonTime(*this).addSeconds(sec);
    }

    CommonTime& operator+=( double sec )
    {
        addSeconds( sec );
        return *this;
    }

    CommonTime operator-(double sec) const {
        CommonTime tempTime = (*this);
        tempTime.addSeconds(-sec);
        return tempTime;
    }

    CommonTime& operator-=(double seconds)
    {
        addSeconds( -seconds );
        return *this;
    };

    CommonTime &addSeconds(double seconds) {
        long days = 0;

        if (abs(seconds) >= SEC_PER_DAY) {
            days = static_cast<long>( seconds * DAY_PER_SEC );
            seconds -= days * SEC_PER_DAY;
        }
        add(days, seconds);
        return *this;
    }

    bool add(long days,
             double sod) {
        m_day += days;
        m_sod += sod;
        return normalize();
    }

    // 归化
    bool normalize() {
        if (abs(m_sod) >= SEC_PER_DAY) {
            long day = m_sod / SEC_PER_DAY;
            m_day += day;
            m_sod -= day * SEC_PER_DAY;
        }
        if (m_sod < 0) {
            m_sod = m_sod + SEC_PER_DAY;
            --m_day;
        }
        return 1;
    }

    std::string toString() const {
        ostringstream oss;
        oss << setfill('0')
            << setw(7) << m_day << " "
            << fixed << setprecision(15) << setw(17) << m_sod
            << " " << m_timeSystem.toString();
        return oss.str();
    }

    bool operator==(const CommonTime &right) const {
        if (m_timeSystem != right.m_timeSystem)
            return false;

        if (m_day == right.m_day && fabs(m_sod - right.m_sod) < std::numeric_limits<double>::epsilon())
            return true;
        else
            return false;
    }

    bool operator!=(const CommonTime &right) const {
        return !operator==(right);
    }

    bool operator<(const CommonTime &right) const {
        if (m_timeSystem != right.m_timeSystem) {
            InvalidRequest ir(
                    "CommonTime objects not in same time system, cannot be compared: "
                    + m_timeSystem.toString() +
                    " != " + right.m_timeSystem.toString());

            throw (ir);
        }

        if (m_day < right.m_day)
            return true;
        if (m_day > right.m_day)
            return false;

        if (m_sod < right.m_sod)
            return true;
        return false;
    }

    bool operator>(const CommonTime &right) const {
        return !operator<=(right);
    }

    bool operator<=(const CommonTime &right) const {
        return (operator<(right) || operator==(right));
    }

    bool operator>=(const CommonTime &right) const {
        return !operator<(right);
    }

    long m_day{};     // Modified Julian Day
    double m_sod{};    // seconds-of-day
    TimeSystem m_timeSystem;

}; // end class CommonTime

// 'julian day' of earliest epoch expressible by CommonTime; 1/1/4713 B.C.
static const CommonTime BEGINNING_OF_TIME = CommonTime(0L, 0.0, TimeSystem::GPS);

// 'julian day' of latest 'julian day' expressible by CommonTime,
// 1/1/4713 A.D.
static const CommonTime END_OF_TIME = CommonTime(3442448L, 0.0, TimeSystem::GPS);

inline std::ostream &operator<<(std::ostream &os, const CommonTime &ct) {
    os << ct.toString();
    return os;
}

// -----------------------------------
// CivilTime
// -----------------------------------
class CivilTime {
public:

    CivilTime(int yr = 0,
              int mo = 0,
              int dy = 0,
              int hr = 0,
              int mn = 0,
              double s = 0.0,
              TimeSystem ts = TimeSystem::GPS)
    {
        year = yr;
        month = mo;
        day = dy;
        hour = hr;
        minute = mn;
        second = s;
        timeSys = ts;  // ✅ 真正赋值！
    }


    /**
     * Copy Constructor.
     */
    CivilTime(const CivilTime &right)
            : year(right.year), month(right.month), day(right.day),
              hour(right.hour), minute(right.minute), second(right.second) {
        timeSys = right.timeSys;
    }

    CivilTime &operator=(const CivilTime &right);

    bool operator==(const CivilTime &right) const;

    std::string toString() const {
        std::ostringstream oss;
        oss << setw(4) << year << "/"
            << setw(2) << month << "/"
            << setw(2) << day << " "
            << setw(2) << hour << ":"
            << setw(2) << minute << ":"
            << setw(2) << second << " "
            << timeSys.toString();

        return oss.str();
    };

    /// Virtual Destructor.
    virtual ~CivilTime() {}

    int year;
    int month;
    int day;
    int hour;
    int minute;
    double second;

    TimeSystem timeSys;
};

inline std::ostream &operator<<(std::ostream &s, const CivilTime &cit) {
    s << cit.toString();
    return s;
}

class JulianDate {
public:

    JulianDate(long double j = 0., TimeSystem ts = TimeSystem::GPS)
            : jd(j) { timeSystem = ts; }

    JulianDate(const JulianDate &right)
            : jd(right.jd) { timeSystem = right.timeSystem; }

    JulianDate &operator=(const JulianDate &right);

    string toString() {
        std::ostringstream oss;
        oss << fixed << setw(16) << jd << ":"
            << timeSystem.toString();

        return oss.str();
    }

    /// Virtual Destructor.
    virtual ~JulianDate() {}

    void reset();

    bool operator==(const JulianDate &right) const;

    bool operator!=(const JulianDate &right) const;

    bool operator<(const JulianDate &right) const;

    long double jd;

    TimeSystem timeSystem;

};

inline std::ostream &operator<<(std::ostream &s, JulianDate &jd) {
    s << jd.toString();
    return s;
}

// year/day_of_year/second_of_day
// 方便用于输出单天解
class YDSTime {
public:
    YDSTime(int y = 0, int d = 0, double s = 0.0,
            TimeSystem ts = TimeSystem::GPS) {
        year = y;
        doy = d;
        sod = s;
        timeSystem = ts;
    }

    YDSTime(const YDSTime &right)
            : year(right.year), doy(right.doy), sod(right.sod) { timeSystem = right.timeSystem; }

    YDSTime &operator=(const YDSTime &right);

    std::string  toString() const {
        std::ostringstream oss;
        oss << setw(4) << year << " "
            << setw(3) << doy << " "
            << setw(14) << sod << " "
            << timeSystem.toString() << " ";

        return oss.str();
    };

    // Virtual Destructor.
    virtual ~YDSTime() {}

    virtual void reset();

    bool operator==(const YDSTime &right) const;

    bool operator!=(const YDSTime &right) const;

    bool operator<(const YDSTime &right) const;

    int year;
    int month;
    int doy;
    double sod;
    TimeSystem timeSystem;
};

inline std::ostream &operator<<(std::ostream &s, const YDSTime &yt) {
    s << yt. toString();
    return s;
};

class MJD
{
public:

    MJD( long double m = 0.,
         TimeSystem ts = TimeSystem::GPS )
            : mjd( m )
    { timeSystem = ts; }


    MJD( const MJD& right )
            : mjd( right.mjd )
    { timeSystem = right.timeSystem; }


    MJD& operator=( const MJD& right );

    /// Virtual Destructor.
    virtual ~MJD()
    {}

    void reset();

    std::string toString()
    {
        std::ostringstream oss;
        oss << setw(8) << mjd
            << timeSystem.toString();

        return oss.str();
    };

    bool operator==(const MJD& right) const;
    bool operator!=(const MJD& right) const;
    bool operator<(const MJD& right) const;
    bool operator>(const MJD& right) const;
    bool operator<=(const MJD& right) const;
    bool operator>=(const MJD& right) const;

    long double mjd;
    TimeSystem timeSystem;
};

inline std::ostream& operator<<( std::ostream& s, MJD& mjd)
{
    s << mjd.toString();
    return s;
}

// JD2020：以 2020-01-01 00:00:00 为 0 点的简化儒略日
class JD2020
{
public:
    JD2020(long double j = 0., TimeSystem ts = TimeSystem::GPS)
        : jd2020(j) { timeSystem = ts; }

    JD2020(const JD2020& right)
        : jd2020(right.jd2020) { timeSystem = right.timeSystem; }

    JD2020& operator=(const JD2020& right);

    void reset() {
        jd2020 = 0.0L;
        timeSystem = TimeSystem::GPS;
    }

    string toString() {
        std::ostringstream oss;
        oss << fixed << setw(16) << jd2020 << " [JD2020] " << timeSystem.toString();
        return oss.str();
    }

    bool operator==(const JD2020& right) const;
    bool operator!=(const JD2020& right) const;
    bool operator<(const JD2020& right) const;
    bool operator>(const JD2020& right) const;
    bool operator<=(const JD2020& right) const;
    bool operator>=(const JD2020& right) const;

public:
    long double   jd2020;      // JD2020 = 0 对应 2020-01-01 00:00:00
    TimeSystem    timeSystem;
};

// 输出运算符
inline std::ostream& operator<<(std::ostream& s, JD2020& jd2020) {
    s << jd2020.toString();
    return s;
}

/// This class encapsulates the "Full Week and Seconds-of-week"
/// time representation.
class WeekSecond
{
public:

    /**
     * Default Constructor.
     * All elements are initialized to zero.
     */
    WeekSecond(unsigned int w = 0,
               double s = 0.,
               TimeSystem ts = TimeSystem::GPS)
            : week(w), sow(s)
    { timeSystem = ts; }

    /**
     * Copy Constructor.
     * @param right a reference to the WeekSecond object to copy
     */
    WeekSecond( const WeekSecond& right )
            : week( right.week ), sow( right.sow )
    { timeSystem = right.timeSystem; }

    /**
     * Assignment Operator.
     * @param right a const reference to the WeekSecond to copy
     * @return a reference to this WeekSecond
     */
    WeekSecond& operator=( const WeekSecond& right );

    /// Virtual Destructor.
    virtual ~WeekSecond()
    {}

    // Return the number of bits in the bitmask used to get the ModWeek from the
    // full week.
    // This is pure virtual and must be implemented in the derived class;
    // e.g. GPSWeek::Nbits(void) { static const int n=10; return n; }
    virtual int Nbits(void) const = 0;

    // Return the bitmask used to get the ModWeek from the full week.
    // This is pure virtual and must be implemented in the derived class;
    // e.g. GPSWeek::bitmask(void) { static const int bm=0x3FF; return bm; }
    virtual int bitmask(void) const = 0;

    // Return the maximum Nbit-week-number minus 1, i.e. the week number at
    // which rollover occurs.
    virtual int rollover(void) const
    { return bitmask()+1; }

    virtual long MJDEpoch(void) const = 0;

    inline virtual unsigned int getDayOfWeek() const
    {
        return static_cast<unsigned int>(sow) / SEC_PER_DAY;
    }

    inline double getSOW() const { return sow; }

    void reset();

    /// @defgroup wsco WeekSecond Comparison Operators
    /// All comparison operators have a parameter "right" which corresponds
    /// to the WeekSecond object to the right of the symbol.
    /// All comparison operators are const and return true on success
    /// and false on failure.
    //@{
    bool operator==( const WeekSecond& right ) const;
    bool operator!=( const WeekSecond& right ) const;
    bool operator<( const WeekSecond& right ) const;
    bool operator>( const WeekSecond& right ) const;
    bool operator<=( const WeekSecond& right ) const;
    bool operator>=( const WeekSecond& right ) const;

    inline virtual void setEpoch(unsigned int e)
    {
        week &= bitmask();
        week |= e << Nbits();
    }

    inline virtual void setModWeek(unsigned int w)
    {
        week &= ~bitmask();
        week |= w & bitmask();
    }

    inline virtual void setEpochModWeek(unsigned int e,
                                        unsigned int w)
    {
        setEpoch(e);
        setModWeek(w);
    }
    inline virtual unsigned int getWeek() const
    {
        return week;
    }

    inline virtual unsigned int getModWeek() const
    {
        return (week & bitmask());
    }

    inline virtual unsigned int getEpoch() const
    {
        return (week >> Nbits());
    }

    inline virtual void getEpochModWeek(unsigned int& e,
                                        unsigned int& w) const
    {
        e = getEpoch();
        w = getModWeek();
    }

    std::string  toString()
    {
        std::ostringstream oss;
        oss << setw(4) << week
            << setw(10) << sow
            << timeSystem. toString();

        return oss.str();
    }

    //@}
    int week;
    double sow;
    TimeSystem timeSystem;
};

inline std::ostream& operator<<( std::ostream& s,  WeekSecond& ws )
{
    s << ws. toString();
    return s;
}


/// This class handles GPS Week and Seconds-of-week. It inherits
/// WeekSecond
/// The GPS week is specified by 10-bit ModWeek, rollover at
/// 1024, bitmask 0x3FF and epoch GPS_EPOCH_MJD
class GPSWeekSecond : public WeekSecond
{
public:

    /// Constructor.
    GPSWeekSecond(unsigned int w = 0,
                  double s = 0.,
                  TimeSystem ts = TimeSystem::GPS)
            : WeekSecond(w,s)
    { timeSystem = ts; }

    /// Constructor from CommonTime

    /// Destructor.
    ~GPSWeekSecond() {}

    /// Return the number of bits in the bitmask used to get the
    /// ModWeek from the full week.
    int Nbits(void) const
    {
        static const int n=10;
        return n;
    }

    /// Return the bitmask used to get the ModWeek from the full week.
    int bitmask(void) const
    {
        static const int bm=0x3FF;
        return bm;
    }

    /// Return the Modified Julian Date (MJD) of epoch for this system.
    long MJDEpoch(void) const
    {
        static const long e=GPS_EPOCH_MJD;
        return e;
    }

    inline bool operator==( const GPSWeekSecond& right ) const
    {
        return WeekSecond::operator==( right );
    }
    inline bool operator!=( const GPSWeekSecond& right ) const
    {
        return WeekSecond::operator!=( right );
    }
    inline bool operator<( const GPSWeekSecond& right ) const
    {
        return WeekSecond::operator<( right );
    }
    inline bool operator>( const GPSWeekSecond& right ) const
    {
        return WeekSecond::operator>( right );
    }
    inline bool operator<=( const GPSWeekSecond& right ) const
    {
        return WeekSecond::operator<=( right );
    }
    inline bool operator>=( const GPSWeekSecond& right ) const
    {
        return WeekSecond::operator>=( right );
    }


}; // end class GPSWeekSecond

/// 北斗周秒类（继承 WeekSecond）
/// BDS 起始历元：2006-01-01 00:00:00 UTC → MJD = 53736
class BDSWeekSecond : public WeekSecond {
public:
    /// 构造函数
    BDSWeekSecond(unsigned int w = 0,
                  double s = 0.,
                  TimeSystem ts = TimeSystem::BDT)
            : WeekSecond(w, s) {
        timeSystem = ts;
    }

    /// 析构函数
    ~BDSWeekSecond() override {}

    /// 周数掩码位数（北斗为 13 位，周数循环 8192 周）
    int Nbits(void) const override {
        static const int n = 13;
        return n;
    }

    /// 周数掩码
    int bitmask(void) const override {
        static const int bm = 0x1FFF; // 2^13 - 1 = 8191
        return bm;
    }

    /// 北斗起始历元 MJD（2006-01-01 00:00:00 UTC）
    long MJDEpoch(void) const override {
        static const long e = 53736;
        return e;
    }

    // 继承比较运算符
    inline bool operator==(const BDSWeekSecond& right) const {
        return WeekSecond::operator==(right);
    }
    inline bool operator!=(const BDSWeekSecond& right) const {
        return WeekSecond::operator!=(right);
    }
    inline bool operator<(const BDSWeekSecond& right) const {
        return WeekSecond::operator<(right);
    }
    inline bool operator>(const BDSWeekSecond& right) const {
        return WeekSecond::operator>(right);
    }
    inline bool operator<=(const BDSWeekSecond& right) const {
        return WeekSecond::operator<=(right);
    }
    inline bool operator>=(const BDSWeekSecond& right) const {
        return WeekSecond::operator>=(right);
    }
};



#endif //TimeStruct_H
