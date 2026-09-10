#ifndef NavEphBDS_HPP
#define NavEphBDS_HPP

#include "NavEphGPS.hpp"

class NavEphBDS : public NavEphGPS {
public:
    NavEphBDS(void) {
        // 北斗时间系统 BDT
        beginValid.m_timeSystem = TimeSystem::BDT;
        endValid.m_timeSystem   = TimeSystem::BDT;
        ctToc.m_timeSystem      = TimeSystem::BDT;
        ctToe.m_timeSystem      = TimeSystem::BDT;
        transmitTime.m_timeSystem = TimeSystem::BDT;
    }

    virtual ~NavEphBDS(void) {}

    // 北斗卫星位置计算
    Xvt svXvt(const CommonTime &t) const ;

    // GEO 卫星专用
    Xvt svXvtGEO(const CommonTime &t) const;

    // 打印北斗星历
    void printData() const;

    // BDS 独有参数
    double BDSWeek;
    double TGD1;
    double TGD2;
    double AODC;
    double AODE;
};

#endif