#include <iostream>
#include <iomanip>
#include "TimeConvert.h"
#include "TimeStruct.h"

using namespace std;

int main() {
    try {
        int week;
        double sow;

        cout << "=========================================\n";
        cout << "           BDS WS => JD2020\n";
        cout << "=========================================\n";
        cout << "Week: "; cin >> week;
        cout << "SOW : "; cin >> sow;

        // 1. 北斗周秒 → CommonTime
        BDSWeekSecond bdsWS(week, sow, TimeSystem::BDT);
        CommonTime ct_BDT;
        BDSWeekSecond2CommonTime(bdsWS, ct_BDT);

        // 2. 转 GPST 系统
        CommonTime ct_GPS = convertTimeSystem(ct_BDT, TimeSystem::GPS);

        // 3. 核心：GPS CommonTime → JD2020
        JD2020 jd2020;
        CommonTime2JD2020(ct_GPS, jd2020);

        // 4. 转回验证
        CommonTime ct_back;
        JD20202CommonTime(jd2020, ct_back);

        // 5. 时间输出
        CivilTime bdtCivil = CommonTime2CivilTime(ct_BDT);
        CivilTime gpsCivil = CommonTime2CivilTime(ct_GPS);

        // ===================== 【使用你修复好的库函数】UTC 计算 =====================
        CommonTime utcCommon = convertTimeSystem(ct_GPS, TimeSystem::UTC);
        CivilTime utcCivil = CommonTime2CivilTime(utcCommon);

        // ===================== 最终输出 =====================
        cout << "\n======================= RESULTS =========================" << endl;

        // JD2020 正确显示 1827.333333
        cout << "JD2020               : "
             << fixed << setprecision(6) << jd2020.jd2020 << "  [GPS]" << endl;

        // CommonTime(GPST) 60676 28800.000000
        cout << "CommonTime (GPST)    : "
             << ct_GPS.m_day << " "
             << fixed << setprecision(6) << ct_GPS.m_sod << "  [GPS]" << endl;

        // BDT
        cout << "BD Time (BDT)       : " << bdtCivil.year << "/" << setw(2) << setfill('0') << bdtCivil.month
             << "/" << setw(2) << setfill('0') << bdtCivil.day << " "
             << setw(2) << setfill('0') << bdtCivil.hour << ":"
             << setw(2) << setfill('0') << bdtCivil.minute << ":"
             << fixed << setprecision(2) << bdtCivil.second << "  [BDT]" << endl;

        // GPS
        cout << "GPS Time            : " << gpsCivil.year << "/" << setw(2) << setfill('0') << gpsCivil.month
             << "/" << setw(2) << setfill('0') << gpsCivil.day << " "
             << setw(2) << setfill('0') << gpsCivil.hour << ":"
             << setw(2) << setfill('0') << gpsCivil.minute << ":"
             << fixed << setprecision(2) << gpsCivil.second << "  [GPS]" << endl;

        // UTC
        cout << "UTC Time            : " << utcCivil.year << "/" << setw(2) << setfill('0') << utcCivil.month
             << "/" << setw(2) << setfill('0') << utcCivil.day << " "
             << setw(2) << setfill('0') << utcCivil.hour << ":"
             << setw(2) << setfill('0') << utcCivil.minute << ":"
             << fixed << setprecision(2) << utcCivil.second << "  [UTC]" << endl;

         // 反向验证输出
         cout << "\n======================= REVERSE VERIFICATION =========================" << endl;
         cout << "Original CommonTime (GPST): " << ct_GPS.m_day << " " << fixed << setprecision(6) << ct_GPS.m_sod << endl;
         cout << "JD2020 => CommonTime        : " << ct_back.m_day << " " << fixed << setprecision(6) << ct_back.m_sod << endl;
         double diff = (ct_GPS.m_day - ct_back.m_day) * 86400.0 + (ct_GPS.m_sod - ct_back.m_sod);
         cout << "Conversion Error           : " << fixed << setprecision(9) << diff << " seconds" << endl;
         cout << "=====================================================================" << endl;

    } catch (const InvalidRequest& e) {
        cerr << "Error: " << e.what() << endl;
        return -1;
    }

    return 0;
}