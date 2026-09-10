#include <iostream>
#include <iomanip>
#include "TimeConvert.h"
#include "TimeStruct.h"

using namespace std;

int main()
{
    try
    {
        int week;
        double sow;

        cout << "==========================================================" << endl;
        cout << "          BDS Week Second => CommonTime & All Times          " << endl;
        cout << "==========================================================" << endl;
        cout << "Input BDS Week and Second of Week:" << endl;
        cout << "Week: ";
        cin >> week;
        cout << "SOW: ";
        cin >> sow;

        // 1. 输入北斗周秒 （库类）
        BDSWeekSecond bdsWS(week, sow, TimeSystem::BDT);

        // 2. 转 CommonTime (BDT) （库函数）
        CommonTime ct_BDT;
        BDSWeekSecond2CommonTime(bdsWS, ct_BDT);

        // 3. BDT → GPS （库函数）
        CommonTime ct_GPS = convertTimeSystem(ct_BDT, TimeSystem::GPS);

        // 4. GPS → UTC （库函数！已经正确两次跳秒！）
        CommonTime ct_UTC = convertTimeSystem(ct_GPS, TimeSystem::UTC);

        // 5. 转成可视时间
        CivilTime gpsCivil = CommonTime2CivilTime(ct_GPS);
        CivilTime bdtCivil = CommonTime2CivilTime(ct_BDT);
        CivilTime utcCivil = CommonTime2CivilTime(ct_UTC);

        // ====================== 输出 ======================
        cout << endl;
        cout << "==========================================================" << endl;
        cout << "Conversion Results:" << endl;
        cout << "==========================================================" << endl;

        cout << "CommonTime (MJD+SoD)  : "
             << ct_GPS.m_day << " "
             << fixed << setprecision(6) << ct_GPS.m_sod
             << "  [GPST]" << endl;

        double mjd_gps = ct_GPS.m_day + ct_GPS.m_sod / 86400.0;
        cout << "MJD (Decimal)         : "
             << fixed << setprecision(6) << mjd_gps
             << "  [GPST]" << endl;

        cout << "BD Time (BDT)         : " << bdtCivil.year << "/" << setw(2) << setfill('0') << bdtCivil.month
             << "/" << setw(2) << setfill('0') << bdtCivil.day << " "
             << setw(2) << setfill('0') << bdtCivil.hour << ":"
             << setw(2) << setfill('0') << bdtCivil.minute << ":"
             << fixed << setprecision(2) << bdtCivil.second << "  [BDT]" << endl;

        cout << "GPS Time              : " << gpsCivil.year << "/" << setw(2) << setfill('0') << gpsCivil.month
             << "/" << setw(2) << setfill('0') << gpsCivil.day << " "
             << setw(2) << setfill('0') << gpsCivil.hour << ":"
             << setw(2) << setfill('0') << gpsCivil.minute << ":"
             << fixed << setprecision(2) << gpsCivil.second << "  [GPST]" << endl;

        cout << "UTC Time              : " << utcCivil.year << "/" << setw(2) << setfill('0') << utcCivil.month
             << "/" << setw(2) << setfill('0') << utcCivil.day << " "
             << setw(2) << setfill('0') << utcCivil.hour << ":"
             << setw(2) << setfill('0') << utcCivil.minute << ":"
             << fixed << setprecision(2) << utcCivil.second << "  [UTC]" << endl;

        cout << "==========================================================" << endl;
    }
    catch (const InvalidRequest& e)
    {
        cerr << "Error: " << e.what() << endl;
        return -1;
    }

    return 0;
}