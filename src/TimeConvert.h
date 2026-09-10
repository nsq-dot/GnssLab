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
#pragma once

#include "TimeStruct.h"

// 读取跳秒
double getLeapSeconds(const CommonTime &ct);

// 时间系统转换
CommonTime convertTimeSystem(
        const CommonTime &ct,
        const TimeSystem &targetSys);

/**
 * convert from "Julian day" (= JD + 0.5)to calendar day.
 */
void convertJD2YMD(double jd,
                   int &iyear,
                   int &imonth,
                   int &iday);

/**
 *   convert calendar day to "Julian day"(= JD + 0.5)
 */
double convertYMD2JD(int iyear,
                     int imonth,
                     int iday);

/** Fundamental routine to convert seconds of day to H:M:S
 */
void convertSOD2HMS(double sod,
                    int &hh,
                    int &mm,
                    double &sec);

/** Fundamental routine to convert H:M:S to seconds of day
 * @param hh integer hour (0 <= hh < 24) (input)
 * @param mm integer minutes (0 <= mm < 60) (input)
 * @param sec double seconds (0 <= sec < 60.0) (input)
 * @return sod seconds of day (input)
 */
double convertHMS2SOD(int hh,
                      int mm,
                      double sec);

CommonTime CivilTime2CommonTime(const CivilTime &civilt);
CivilTime CommonTime2CivilTime(const CommonTime &ct);
CommonTime JulianDate2CommonTime(JulianDate &jd);
JulianDate CommonTime2JulianDate(CommonTime &ct);
void CommonTime2MJD(const CommonTime& ct, MJD& mjd );
void MJD2CommonTime(MJD& mjd, CommonTime& ct);
CommonTime YDSTime2CommonTime(YDSTime &ydst);
YDSTime CommonTime2YDSTime(const CommonTime &ct);
void CommonTime2WeekSecond(const CommonTime& ct, WeekSecond& wk);
void WeekSecond2CommonTime(WeekSecond& wk, CommonTime& ct);
// BDSWeekSecond 与 CommonTime 互转
void BDSWeekSecond2CommonTime(BDSWeekSecond& bdsWs, CommonTime& ct);
void CommonTime2BDSWeekSecond(const CommonTime& ct, BDSWeekSecond& bdsWs);
void CommonTime2JD2020(const CommonTime& ct, JD2020& jd2020);
void JD20202CommonTime(JD2020& jd2020, CommonTime& ct);