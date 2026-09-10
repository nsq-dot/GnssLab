#include <iostream>
#include <cstdio>
#include "TimeConvert.h"
#include "TimeStruct.h"

using namespace std;

// ======================================================================
//  解析 YYYY/MM/DD HH:MM:SS 格式时间
// ======================================================================
void parseTime(const string& dateStr, const string& timeStr,
               int& year, int& month, int& day,
               int& hour, int& min, double& sec)
{
    sscanf(dateStr.c_str(), "%d/%d/%d", &year, &month, &day);
    sscanf(timeStr.c_str(), "%d:%d:%lf", &hour, &min, &sec);
}

// ======================================================================
// 【主程序】调用库函数 convertTimeSystem 实现 GPS → UTC
// ======================================================================
int main()
{
    try {
        int y, m, d, h, mi;
        double s;
        string dateStr, timeStr;

        cout << "==========================================================" << endl;
        cout << "                    GPS to UTC Converter                  " << endl;
        cout << "==========================================================" << endl;
        cout << "Input format: YYYY/MM/DD HH:MM:SS\n";
        cout << "Input: ";
        cin >> dateStr >> timeStr;

        // 解析输入时间
        parseTime(dateStr, timeStr, y, m, d, h, mi, s);

        // 构建 GPS 时间
        CivilTime gpsCivil(y, m, d, h, mi, s, TimeSystem::GPS);
        CommonTime gpsTime = CivilTime2CommonTime(gpsCivil);

        // ======================================================
        CommonTime utcTime = convertTimeSystem(gpsTime, TimeSystem::UTC);

        // 转成可读时间
        CivilTime utcCivil = CommonTime2CivilTime(utcTime);
        utcCivil.timeSys = TimeSystem::UTC;

        // 输出
        cout << "\n=========================================================" << endl;
        cout << "                        RESULTS                        " << endl;
        cout << "=========================================================" << endl;
        cout << "GPS  Time : " << gpsCivil << endl;
        cout << "UTC  Time : " << utcCivil << endl;
        cout << "=========================================================" << endl;

    } catch (const InvalidRequest& e) {
        cerr << "Error: " << e.what() << endl;
        return -1;
    }

    return 0;
}