#include "RinexNavStore.hpp"
#include "StringUtils.h"

using namespace std;
#define debug 0

const string RinexNavStore::stringVersion = "RINEX VERSION / TYPE";
const string RinexNavStore::stringRunBy = "PGM / RUN BY / DATE";
const string RinexNavStore::stringComment = "COMMENT";
const string RinexNavStore::stringIonoCorr = "IONOSPHERIC CORR";
const string RinexNavStore::stringTimeSysCorr = "TIME SYSTEM CORR";
const string RinexNavStore::stringLeapSeconds = "LEAP SECONDS";
//R2.10GLO
const string RinexNavStore::stringCorrSysTime = "CORR TO SYSTEM TIME";
//R2.11GPS
const string RinexNavStore::stringDeltaUTC = "DELTA-UTC: A0,A1,T,W";
//R2.11GEO
const string RinexNavStore::stringDUTC = "D-UTC A0,A1,T,W,S,U";
//R2.11
const string RinexNavStore::stringIonAlpha = "ION ALPHA";
//R2.11
const string RinexNavStore::stringIonBeta = "ION BETA";
const string RinexNavStore::stringEoH = "END OF HEADER";

void RinexNavStore::loadGPSEph(NavEphGPS &gpsEph, string &line, fstream &navFileStream) {

    SatID sat(line.substr(0,3));

    ///add each sat into the satTable
    vector<SatID>::iterator result = find(satTable.begin(), satTable.end(), sat);
    if (result == satTable.end()) {
        satTable.push_back(sat);
    }

    int yr = safeStoi(line.substr(4, 4));
    int mo = safeStoi(line.substr(9, 2));
    int day = safeStoi(line.substr(12, 2));
    int hr = safeStoi(line.substr(15, 2));
    int min = safeStoi(line.substr(18, 2));
    double sec = safeStod(line.substr(21, 2));

    /// Fix RINEX epochs of the form 'yy mm dd hr 59 60.0'
    short ds = 0;
    if (sec >= 60.) {
        ds = sec;
        sec = 0;
    }

    CivilTime cvt(yr, mo, day, hr, min, sec);
    gpsEph.CivilToc = cvt;
//      gpsEph.ctToe = cvt.convertToCommonTime();
    gpsEph.ctToe = CivilTime2CommonTime(cvt);;

    if (ds != 0) gpsEph.ctToe += ds;
    gpsEph.ctToe.setTimeSystem(TimeSystem::GPS);

    GPSWeekSecond gws;
    CommonTime2WeekSecond(gpsEph.ctToe, gws);     // sow is system-independent

    gpsEph.Toc = gws.sow;
    gpsEph.af0 = safeStod(line.substr(23, 19));
    gpsEph.af1 = safeStod(line.substr(42, 19));
    gpsEph.af2 = safeStod(line.substr(61, 19));

    ///orbit-1
    int n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.IODE = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.Crs = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.Delta_n = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.M0 = safeStod(line.substr(n, 19));
    ///orbit-2
    n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.Cuc = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.ecc = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.Cus = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.sqrt_A = safeStod(line.substr(n, 19));
    ///orbit-3
    n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.Toe = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.Cic = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.OMEGA_0 = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.Cis = safeStod(line.substr(n, 19));
    ///orbit-4
    n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.i0 = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.Crc = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.omega = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.OMEGA_DOT = safeStod(line.substr(n, 19));
    ///orbit-5
    n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.IDOT = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.L2Codes = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.GPSWeek = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.L2Pflag = safeStod(line.substr(n, 19));
    ///orbit-6
    n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.URA = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.SV_health = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.TGD = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.IODC = safeStod(line.substr(n, 19));
    ///orbit-7
    n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    gpsEph.HOWtime = safeStod(line.substr(n, 19));
    n += 19;
    gpsEph.fitInterval = safeStod(line.substr(n, 19));
    n += 19;

    /// some process
    /// Some RINEX files have HOW < 0.
    while (gpsEph.HOWtime < 0) {
        gpsEph.HOWtime += (long) FULLWEEK;
        gpsEph.GPSWeek--;
    }

    /// In RINEX *files*, weeknum is the week of TOE.
    /// Internally (Rx3NavData), weeknum is week of HOW
    if (gpsEph.HOWtime - gpsEph.Toe > HALFWEEK)
        gpsEph.GPSWeek--;
    else if (gpsEph.HOWtime - gpsEph.Toe < -HALFWEEK)
        gpsEph.GPSWeek++;

    /// Get week for clock, to build Toc
    long adjHOWtime = gpsEph.HOWtime;
    short adjWeeknum = gpsEph.GPSWeek;
    long lToc = (long) gpsEph.Toc;
    if ((gpsEph.HOWtime % SEC_PER_DAY) == 0 &&
        ((lToc) % SEC_PER_DAY) == 0 &&
        gpsEph.HOWtime == lToc) {
        adjHOWtime = gpsEph.HOWtime - 30;
        if (adjHOWtime < 0) {
            adjHOWtime += FULLWEEK;
            adjWeeknum--;
        }
    }

    double dt = gpsEph.Toc - adjHOWtime;
    int week = gpsEph.GPSWeek;
    if (dt < -HALFWEEK) week++; else if (dt > HALFWEEK) week--;
    GPSWeekSecond gws2 = GPSWeekSecond(week, gpsEph.Toc, TimeSystem::GPS);
//      gpsEph.ctToc = GPSWeekSecond(week, gpsEph.Toc, TimeSystem::GPS);
    WeekSecond2CommonTime(gws2, gpsEph.ctToc);

    gpsEph.ctToc.setTimeSystem(TimeSystem::GPS);

    gpsEphData[sat][gpsEph.ctToe] = gpsEph;
}

// 【北斗星历读取函数】
void RinexNavStore::loadBDSEph(NavEphBDS& bdsEph, std::string& line, std::fstream& navFileStream) {
    SatID sat(line.substr(0,3));

    vector<SatID>::iterator result = find(satTable.begin(), satTable.end(), sat);
    if (result == satTable.end()) {
        satTable.push_back(sat);
    }

    int yr = safeStoi(line.substr(4, 4));
    int mo = safeStoi(line.substr(9, 2));
    int day = safeStoi(line.substr(12, 2));
    int hr = safeStoi(line.substr(15, 2));
    int min = safeStoi(line.substr(18, 2));
    double sec = safeStod(line.substr(21, 2));

    CivilTime cvt(yr, mo, day, hr, min, sec);
    bdsEph.CivilToc = cvt;
    bdsEph.ctToe = CivilTime2CommonTime(cvt);
    bdsEph.ctToe.setTimeSystem(TimeSystem::BDT);

    BDSWeekSecond bws;
    CommonTime2BDSWeekSecond(bdsEph.ctToe, bws);
    bdsEph.Toc = bws.sow;

    bdsEph.af0 = safeStod(line.substr(23, 19));
    bdsEph.af1 = safeStod(line.substr(42, 19));
    bdsEph.af2 = safeStod(line.substr(61, 19));

    int n = 4;
    getline(navFileStream, line);
    replace(line.begin(), line.end(), 'D', 'e');
    bdsEph.IODE = safeStod(line.substr(n,19)); n+=19;
    bdsEph.Crs  = safeStod(line.substr(n,19)); n+=19;
    bdsEph.Delta_n = safeStod(line.substr(n,19)); n+=19;
    bdsEph.M0   = safeStod(line.substr(n,19));

    n=4; getline(navFileStream, line); replace(line.begin(), line.end(), 'D','e');
    bdsEph.Cuc = safeStod(line.substr(n,19)); n+=19;
    bdsEph.ecc = safeStod(line.substr(n,19)); n+=19;
    bdsEph.Cus = safeStod(line.substr(n,19)); n+=19;
    bdsEph.sqrt_A = safeStod(line.substr(n,19));

    n=4; getline(navFileStream, line); replace(line.begin(), line.end(), 'D','e');
    bdsEph.Toe = safeStod(line.substr(n,19)); n+=19;
    bdsEph.Cic = safeStod(line.substr(n,19)); n+=19;
    bdsEph.OMEGA_0 = safeStod(line.substr(n,19)); n+=19;
    bdsEph.Cis = safeStod(line.substr(n,19));

    n=4; getline(navFileStream, line); replace(line.begin(), line.end(), 'D','e');
    bdsEph.i0 = safeStod(line.substr(n,19)); n+=19;
    bdsEph.Crc = safeStod(line.substr(n,19)); n+=19;
    bdsEph.omega = safeStod(line.substr(n,19)); n+=19;
    bdsEph.OMEGA_DOT = safeStod(line.substr(n,19));

    n=4; getline(navFileStream, line); replace(line.begin(), line.end(), 'D','e');
    bdsEph.IDOT = safeStod(line.substr(n,19)); n+=19;
    bdsEph.L2Codes = safeStod(line.substr(n,19)); n+=19;
    bdsEph.BDSWeek = safeStod(line.substr(n,19)); n+=19;
    bdsEph.L2Pflag = safeStod(line.substr(n,19));

    n=4; getline(navFileStream, line); replace(line.begin(), line.end(), 'D','e');
    bdsEph.URA = safeStod(line.substr(n,19)); n+=19;
    bdsEph.SV_health = safeStod(line.substr(n,19)); n+=19;
    bdsEph.TGD1 = safeStod(line.substr(n,19)); n+=19;
    bdsEph.IODC = safeStod(line.substr(n,19));

    n=4; getline(navFileStream, line); replace(line.begin(), line.end(), 'D','e');
    bdsEph.HOWtime = safeStod(line.substr(n,19)); n+=19;
    bdsEph.fitInterval = safeStod(line.substr(n,19));

    BDSWeekSecond bws2(bdsEph.BDSWeek, bdsEph.Toc, TimeSystem::BDT);
    BDSWeekSecond2CommonTime(bws2, bdsEph.ctToc);
    bdsEph.ctToc.setTimeSystem(TimeSystem::BDT);

    bdsEphData[sat][bdsEph.ctToe] = bdsEph;
}

void RinexNavStore::loadFile(string &file) {
    rx3NavFile = file;
    if (rx3NavFile.size() == 0) {
        cout << "the nav file path is empty!" << endl;
        exit(-1);
    }

    if (debug)
        cout << "RinexNavStore: fileName:" << rx3NavFile << endl;

    fstream navFileStream(rx3NavFile.c_str(), ios::in);
    if (!navFileStream) {
        cerr << "can't open file:" << rx3NavFile << endl;
        exit(-1);
    }

    int lineNumber(0);

    ///first, we should read nav head
    while (1) {
        string line;
        getline(navFileStream, line);

        if (debug)
            cout << "RinexNavStore:" << line << endl;

        stripTrailing(line);

        if (line.length() == 0) continue;
        else if (line.length() < 60) {
            cout << line << endl;
            cout << "line.length is fault" << line.length() << endl;
            FFStreamError e("Invalid line length, \n"
                            "may be the file is generated by windows, \n"
                            "please use dos2unix to convert the file!");
            throw (e);
        }

        lineNumber++;

        string thisLabel(line, 60, 20);

        /// following is huge if else else ... endif for each record type
        if (thisLabel == stringVersion) {
            /// "RINEX VERSION / TYPE"
            version = safeStod(line.substr(0, 20));
            fileType = strip(line.substr(20, 20));
            if(version<3.0)
            {
                FileMissingException e("don't support navigation file with version less than 3.0");
                throw(e);
            }
            if (version >= 3) {                        // ver 3
                if (fileType[0] != 'N' && fileType[0] != 'n') {
                    FFStreamError e("File type is not NAVIGATION: " + fileType);
                    throw(e);
                }
                fileSys = strip(line.substr(40, 20));   // not in ver 2
            }
            fileType = "NAVIGATION";
        } else if (thisLabel == stringRunBy) {
            /// "PGM / RUN BY / DATE"
            fileProgram = strip(line.substr(0, 20));
            fileAgency = strip(line.substr(20, 20));
            // R2 may not have 'UTC' at end
            date = strip(line.substr(40, 20));
        } else if (thisLabel == stringComment) {
            /// "COMMENT"
            commentList.push_back(strip(line.substr(0, 60)));
        } else if (thisLabel == stringIonoCorr) {
            /// "IONOSPHERIC CORR"
            string ionoCorrType = strip(line.substr(0, 4));
            vector<double> ionoCorrCoeff;
            for (int i = 0; i < 4; i++) {
                double ionoCorr = safeStod(line.substr(5 + 12 * i, 12));
                ionoCorrCoeff.push_back(ionoCorr);
            }
            ionoCorrData[ionoCorrType].clear();
            ionoCorrData[ionoCorrType] = ionoCorrCoeff;
        } else if (thisLabel == stringTimeSysCorr) {
            /// "TIME SYSTEM CORR"
            string timeSysCorrType = strip(line.substr(0, 4));

            TimeSysCorr timeSysCorrValue;
            timeSysCorrValue.A0 = safeStod(line.substr(5, 17));
            timeSysCorrValue.A1 = safeStod(line.substr(22, 16));
            timeSysCorrValue.refSOW = safeStoi(line.substr(38, 7));
            timeSysCorrValue.refWeek = safeStoi(line.substr(45, 5));
            timeSysCorrValue.geoProvider = string(" ");
            timeSysCorrValue.geoUTCid = 0;

            timeSysCorrData[timeSysCorrType] = timeSysCorrValue;
        } else if (thisLabel == stringLeapSeconds) {
            /// "LEAP SECONDS"
            leapSeconds = safeStoi(line.substr(0, 6));
            leapDelta = safeStoi(line.substr(6, 6));
            leapWeek = safeStoi(line.substr(12, 6));
            leapDay = safeStoi(line.substr(18, 6));
        } else if (thisLabel == stringEoH) {
            /// "END OF HEADER"
            break;
        }
    }

    ///now, start read nav data
    while (navFileStream.peek() != EOF) {
        string line;
        getline(navFileStream, line);

        if (debug)
            cout << "RinexNavStore:" << line << endl;

        replace(line.begin(), line.end(), 'D', 'e');

        if (line[0] == 'G') {
            NavEphGPS gpsEph;
            loadGPSEph(gpsEph, line, navFileStream);
        }
        else if (line[0] == 'C') {
             NavEphBDS bdsEph;
             loadBDSEph(bdsEph, line, navFileStream);
        }
    }
}

// ===================================================
// 判断是否为北斗 GEO 卫星（C01~C05）
// ===================================================
bool RinexNavStore::ifBDS_GEO(const SatID& sat) const
{
    if (sat.system != "C")
        return false;

    string satStr = sat.toString();   // 例：C01
    int prn = stoi(satStr.substr(1)); // 截取 01 → 转 int 1

    return (prn >= 1 && prn <= 5);     // C01~C05 → GEO
}

Xvt RinexNavStore::getXvt(const SatID &sat, const CommonTime &epoch) {
    Xvt xvt;
    CommonTime realEpoch;
    TimeSystem ts;
    if (debug)
        cout << sat << endl;

    if (sat.system == "G") {
        ts = TimeSystem::GPS;
        realEpoch = convertTimeSystem(epoch, ts);

        if (debug)
            cout << CommonTime2CivilTime(epoch) << endl;

        NavEphGPS gpsEph = findGPSEph(sat, realEpoch);

        if (debug) {
            cout << "RinexNavStore::GPS eph:" << endl;
            gpsEph.printData();
        }

        xvt = gpsEph.svXvt(realEpoch);

        if (debug) {
            cout << "RinexNavStore::xvt:" << endl;
            cout << xvt << endl;
        }
    }

    else if (sat.system == "C") {
        ts = TimeSystem::BDT;
        realEpoch = convertTimeSystem(epoch, ts);
        NavEphBDS bdsEph = findBDSEph(sat, realEpoch);

        // ==============================================
        // 从卫星号自动判断 PRN，C01~C05 = GEO
        // ==============================================
        string satStr = sat.toString();   // 得到 "C01" "C05" "C06" ...
        int prn = 0;

        // 安全提取 PRN
        if (satStr.size() >= 2) {
            string prnStr = satStr.substr(1);  // 取 "01" "05"..."10"
            prn = atoi(prnStr.c_str());        // 转数字 1,5,6...
        }

        // 自动判断：1~5 → GEO，6以上 → 普通
        if (prn >= 1 && prn <= 5) {
            xvt = bdsEph.svXvtGEO(realEpoch);   // GEO 地球静止卫星
        } else {
            xvt = bdsEph.svXvt(realEpoch);      // IGSO / MEO 普通卫星
        }
    }
    else {
        InvalidRequest e("RinexNavStore: don't support the input satellite system!");
        throw (e);
    }

    return xvt;
}

NavEphGPS RinexNavStore::findGPSEph(const SatID &sat, const CommonTime &epoch) {
    GPSWeekSecond targetWS;
    CommonTime2WeekSecond(epoch, targetWS);

    NavEphGPS gpsEph;           // 最终返回的最优星历
    double minDiff = 1e10;       // 记录最小时间差

    // 遍历该卫星的所有广播星历
    for (auto& entry : gpsEphData[sat]) {
        const CommonTime& ephTime = entry.first;
        const NavEphGPS& eph = entry.second;

        GPSWeekSecond ephWS;
        CommonTime2WeekSecond(ephTime, ephWS);

        // 时间差（秒）
        double diff = fabs(ephWS.sow - targetWS.sow);

        // 只保留 ±2 小时内的星历
        if (diff > 7200.0)
            continue;

        // 选择【时间最近】的星历（核心改进！）
        if (diff < minDiff) {
            minDiff = diff;
            gpsEph = eph;
        }
    }

    return gpsEph;
}

NavEphBDS RinexNavStore::findBDSEph(const SatID &sat, const CommonTime &epoch) {
    BDSWeekSecond targetWS;
    CommonTime2BDSWeekSecond(epoch, targetWS);
    NavEphBDS bdsEph;

    // 新增：记录最小时间差
    double minDiff = 1e10;

    for (auto it : bdsEphData[sat]) {
        BDSWeekSecond ws;
        CommonTime2BDSWeekSecond(it.first, ws);
        double diff = ws.sow - targetWS.sow;
        double absDiff = fabs(diff);  // 取绝对值

        // 只考虑 ±2 小时内的星历
        if (absDiff > 7200.0)
            continue;

        // 核心改进：选择【时间最近】的星历，而不是第一个碰到的
        if (absDiff < minDiff) {
            minDiff = absDiff;
            bdsEph = it.second;  // 仍然用原来的变量bdsEph
        }
    }

    return bdsEph;
}

