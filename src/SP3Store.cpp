
#include <iostream>
#include <fstream>
#include <iomanip>

#include "Exception.h"
#include "TimeStruct.h"
#include "StringUtils.h"
#include "SP3Store.hpp"
#include "TimeConvert.h"
#include "MathUtils.hpp"

#define debug 0
using namespace std;

Xvt SP3Store::getXvt(const SatID &sat, const CommonTime &time)
noexcept(false) {
    Xvt xvt;

    PosVel posVel;
    ClockData clkData;

    posVel = getPosVel(sat, time);
    clkData = getClock(sat, time);

    xvt.x = posVel.pos;
    xvt.v = posVel.vel;
    xvt.clkbias = clkData.clockBias;
    xvt.clkdrift = clkData.clockDrift;

    if (debug) {
        cout << setprecision(10) << "pos:" << posVel.pos << "vel:" << posVel.vel << endl;
        cout << setprecision(10) << "bias:" << clkData.clockBias << "drift:" << clkData.clockDrift << endl;
    }

    // 相对论改正
    xvt.relcorr = -2.0 * ((xvt.x[0] / C_MPS) * (xvt.v[0] / C_MPS)
                          + (xvt.x[1] / C_MPS) * (xvt.v[1] / C_MPS)
                          + (xvt.x[2] / C_MPS) * (xvt.v[2] / C_MPS));
    return xvt;

}

PosVel SP3Store::getPosVel(const SatID &sat, const CommonTime &time) {

    // The corresponding time in N half.
    Vector3d Pos, Vel;
    std::map<CommonTime, Vector3d> epochPosData = satPositionData.at(sat);

    int N(9);
    int Nhalf(std::floor(N / 2));
    std::vector<double> times, X, Y, Z;

    std::map<CommonTime, Vector3d>::iterator low, it1;

    low = epochPosData.lower_bound(time);

    bool beginning(false);
    bool ending(false);

    // if at the beginning
    it1 = low;
    for (int i = 0; i < Nhalf; i++) {
        if (it1 == epochPosData.begin()) {
            beginning = true;
            break;
        }
        it1--;
    }

    // if at the end
    it1 = low;
    for (int i = 0; i < Nhalf + 1; i++) {
        if (it1 == epochPosData.end()) {
            ending = true;
            break;
        }
        it1++;
    }

    if (debug)
        cout << "ending:" << ending << endl;

    // 根据三种情况, 给定开始的索引
    if (beginning) {
        it1 = epochPosData.begin();
    } else if (ending) {
        it1 = epochPosData.end();
        for (int i = 0; i < N + 1; i++) {
            it1--;
        }
    } else {
        it1 = low;
        for (int i = 0; i < Nhalf; i++) {
            it1--;
        }
    }

    CommonTime firstEpoch = it1->first;
    if (debug)
        cout << "firstEpoch" << firstEpoch << endl;

    for (int i = 0; i < N; i++) {
        if (debug) {
            cout << "sat:" << sat
                 << "time:" << it1->first
                 << "x:" << it1->second[0]
                 << "y:" << it1->second[1]
                 << "z:" << it1->second[2]
                 << endl;
        }

        times.push_back(it1->first - firstEpoch);
        X.push_back(it1->second[0]);
        Y.push_back(it1->second[1]);
        Z.push_back(it1->second[2]);
        it1++;
    }

    double dt(time - firstEpoch), err;

    if (debug) {
        cout << "dt for pos at time:" << time << ":" << dt << endl;
    }

    LagrangeInterpolation(times, X, dt, Pos[0], Vel[0]);
    LagrangeInterpolation(times, Y, dt, Pos[1], Vel[1]);
    LagrangeInterpolation(times, Z, dt, Pos[2], Vel[2]);

    if (epochPosData.find(time) != epochPosData.end()) {
        Pos = epochPosData[time];
    }

    PosVel posVel;
    posVel.pos = Pos;
    posVel.vel = Vel;

    return posVel;
};

ClockData SP3Store::getClock(const SatID &sat, const CommonTime &time) {
    ClockData clockData;

    // The corresponding time in N half.
    double bias, drift;
    std::map<CommonTime, double> epochBiasData = satClockData.at(sat);


    std::vector<double> times, biasData;
    std::map<CommonTime, double>::iterator low, it1;
    low = epochBiasData.lower_bound(time);

    // 先减去1,得到比给定时间小的位置
    if (low == epochBiasData.begin()) {
        it1 = low;
    } else if (low == epochBiasData.end()) {
        it1 = low;
        it1--;
        it1--;
    } else {
        it1 = low;
        it1--;
    }


    CommonTime firstEpoch = it1->first;
    for (int i = 0; i < 2; i++) {
        times.push_back(it1->first - firstEpoch);
        biasData.push_back(it1->second);
        if (debug) {
            cout << setprecision(14) << "time:" << it1->first
                 << "dt:" << (it1->first - firstEpoch)
                 << "bias:" << it1->second << endl;
        }
        ++it1;
    }

    double dt(time - firstEpoch), err;


    drift = (biasData[1] - biasData[0]) / (times[1] - times[0]);
    bias = biasData[0] + (dt - times[0]) * drift;    // sec/sec
    if (debug) {
        cout << setprecision(14) << "dt for time: " << time << "dt: " << dt << "bias:" << bias << endl;
    }

    if (epochBiasData.find(time) != epochBiasData.end()) {
        bias = epochBiasData[time];
    }

    clockData.clockBias = bias;
    clockData.clockDrift = drift;

    return clockData;
};


// Load an SP3 ephemeris file; may set the velocity and acceleration flags.
// If the clock store uses RINEX clock files, this ignores the clock data.
void SP3Store::loadSP3File(const std::string &filename)
noexcept(false) {
    // open the input stream
    std::fstream strm(filename.c_str(), ios::in);
    if (!strm.is_open()) {
        FileMissingException e("File " + filename + " could not be opened");
        throw (e);
    }

    loadSP3Header(strm);
    loadSP3Data(strm);
    // close
    strm.close();
}

void SP3Store::loadSP3Header(std::fstream &strm) {

    string line;
    getline(strm, line);
    if (debug) std::cout << "SP3 Header Line 1 " << line << std::endl;

    if (line[0] == '#' && line[1] != '#')                  // line 1
    {
        // version character
        version = line[1];

        if (version != 'd') {
            FFStreamError e("sp3 file MUST be sp3d");
            throw (e);
        }

        // parse the rest of the line
        int year = safeStoi(line.substr(3, 4));
        int month = safeStoi(line.substr(8, 2));
        int dom = safeStoi(line.substr(11, 2));
        int hour = safeStoi(line.substr(14, 2));
        int minute = safeStoi(line.substr(17, 2));
        double second = safeStoi(line.substr(20, 11));

        time = CivilTime2CommonTime(CivilTime(year, month, dom, hour, minute, second));

    } else {
        FFStreamError e("Unknown label in line 1: " + line.substr(0, 2));
        throw (e);
    }

    getline(strm, line);
    if (debug) std::cout << "SP3 Header Line 2 " << line << std::endl;

    if (line[0] == '#' && line[1] == '#')                           // line 2
    {
        epochInterval = safeStod(line.substr(24, 14));
    } else {
        FFStreamError e("Unknown label in line 2: " + line.substr(0, 2));
        throw (e);
    }

    if (debug) {
        cout << "epochInterva:" << epochInterval << endl;
    }

    int i, index;
    int numSVs(0), readSVs(0);

    // the map stores them sorted, so use svsAsWritten to determine
    // which SV each accuracy corresponds to.
    vector<SatID> svsAsWritten;
    SatID sat;

    i = 3;

    streampos sp;

    // read in the SV list
    do {
        sp = strm.tellg();

        // read a line
        getline(strm, line);
        if (line[0] == '+' && line[1] == '+') {
            strm.seekg(sp);
            break;
        }

        if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
        if (line[0] == '+' && line[1] != '+') {
            if (i == 3) {
                numSVs = safeStoi(line.substr(3, 3));
                svsAsWritten.resize(numSVs);
            }

            for (index = 9; index < 60; index += 3) {
                if (readSVs < numSVs) {

                    sat = SatID(line.substr(index, 3));
                    svsAsWritten[readSVs] = sat;
                    if (debug) {
                        cout << "SP3Store header:" << sat << endl;
                    }

                    satSet.insert(sat);
                    satList[sat] = 0;
                    readSVs++;
                }
            }
        } else {
            FFStreamError e("Unknown 1st char in line "
                            + std::to_string(i) + ": "
                            + string(1, line[0]));
            throw (e);
        }

        i++;

    } while (true);


    readSVs = 0;
    // read in the accuracy
    do {
        sp = strm.tellg();

        getline(strm, line);
        if (line[0] == '%' && line[1] == 'c') {
            strm.seekg(sp);
            break;
        }

        if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
        if ((line[0] == '+') && (line[1] == '+')) {
            for (index = 9; index < 60; index += 3) {
                if (readSVs < numSVs) {
                    satList[svsAsWritten[readSVs]] = safeStoi(line.substr(index, 3));
                    readSVs++;
                }
            }
        } else {
            FFStreamError e("Unknown label in line "
                            + std::to_string(i) + ": "
                            + line.substr(0, 2));
            throw (e);
        }

        i++;

    } while (true);


    getline(strm, line);
    if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
    if (version == 'c' || version == 'd') {
        if (line[0] == '%' && line[1] == 'c')                         // line 13
        {
            // file system
            system=(line.substr(3, 1));

            // time system
            string ts = (line.substr(9, 3));
            timeSystem = TimeSystem(ts);
        } else {
            FFStreamError e("Unknown label in line "
                            + std::to_string(i) + ": "
                            + line.substr(0, 2));
            throw (e);
        }
    }
    i++;

    getline(strm, line);
    if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
    i++;

    getline(strm, line);
    if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
    if (version == 'c' || version == 'd') {
        if (line[0] == '%' && line[1] == 'f')                           // line 15
        {
            basePV = safeStod(line.substr(3, 10));
            baseClk = safeStod(line.substr(14, 12));
        } else {
            FFStreamError e("Unknown label in line "
                            + std::to_string(i) + ": "
                            + line.substr(0, 2));
            throw (e);
        }
    }
    i++;

    getline(strm, line);                                // line 16
    if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
    i++;

    // read in 2 unused %i lines                             // lines 17,18
    for (int j = 0; j <= 1; j++) {
        getline(strm, line);
        if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
        i++;
    }

    // read in 4 comment lines
    comments.clear();
    do {
        sp = strm.tellg();

        getline(strm, line);

        if (line[0] == '*') {
            strm.seekg(sp);
            break;
        }

        if (debug) std::cout << "SP3 Header Line " << i << " " << line << std::endl;
        // strip the first 3 characters
        line.erase(0, 3);
        // and add to the comment vector
        comments.push_back(line);

        i++;

    } while (true);
}

void SP3Store::loadSP3Data(std::fstream &strm) {
    while (1) {

        // read next line into the lastLine
        getline(strm, lastLine);
        if (debug) {
            cout << "sp3 data record:" << lastLine << endl;
        }

        if (strm.eof()) {
            break;
        }

        if (lastLine.substr(0, 3) == string("EOF")) {
            break;
        }

            // Epoch line read
        else if (lastLine[0] == '*') {
            int year = safeStoi(lastLine.substr(3, 4));
            int month = safeStoi(lastLine.substr(8, 2));
            int dom = safeStoi(lastLine.substr(11, 2));
            int hour = safeStoi(lastLine.substr(14, 2));
            int minute = safeStoi(lastLine.substr(17, 2));
            double second = safeStoi(lastLine.substr(20, 10));

            CivilTime t;
            t = CivilTime(year, month, dom, hour, minute, second, timeSystem);
            currentEpoch = CivilTime2CommonTime(t);
        }
            // P or V record read
        else if (lastLine[0] == 'P') {
            // parse the line
            sat = SatID(lastLine.substr(1, 3));

            // convert km -> m
            Vector3d pos;
            pos[0] = safeStod(lastLine.substr(4, 14)) * 1000.0;
            pos[1] = safeStod(lastLine.substr(18, 14)) * 1000.0;
            pos[2] = safeStod(lastLine.substr(32, 14)) * 1000.0;

            satPositionData[sat][currentEpoch] = pos;

            if (debug) {
                cout << "sat:" << sat << "time:" << currentEpoch << "pos" << pos << endl;
            }

            // convert microsec -> sec;
            double clk;
            clk = safeStod(lastLine.substr(46, 14)) * 1.e-6;
            satClockData[sat][currentEpoch] = clk;

            if (debug) {
                cout << setprecision(14) << fixed;
                cout << "sat:" << sat << "time:" << currentEpoch << "clk " << clk << endl;
            }

        } else {
            continue;
        }
    }  // end while loop processing records
}

// Load a RINEX clock file; may set the 'have' bias and drift flags
void SP3Store::loadRinexClockFile(const std::string &filename)
noexcept(false) {
    clockReader.read(filename);
    satClockData.clear();
    // 当前文件中的filename插入到sp3Store中的satClockData;
    for (auto std: clockReader.satClockData) {
        for (auto td: std.second) {
            satClockData[std.first][td.first] = td.second;
        }
    }
}


