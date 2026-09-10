
#include "Rx3ClockReader.hpp"
#include "StringUtils.h"
#include "TimeStruct.h"
#include "GnssStruct.h"
#include "TimeStruct.h"

#include <fstream>
#include <iostream>
#include <map>

using namespace std;

#define debug 0

const string Rx3ClockReader::versionString = "RINEX VERSION / TYPE";
const string Rx3ClockReader::runByString = "PGM / RUN BY / DATE";
const string Rx3ClockReader::intervalString = "INTERVAL";
const string Rx3ClockReader::commentString = "COMMENT";
const string Rx3ClockReader::sysString = "SYS / # / OBS TYPES";
const string Rx3ClockReader::timeSystemString = "TIME SYSTEM ID";
const string Rx3ClockReader::leapSecondsString = "LEAP SECONDS";
const string Rx3ClockReader::leapSecondsGNSSString = "LEAP SECONDS GNSS";
const string Rx3ClockReader::sysDCBString = "SYS / DCBS APPLIED";
const string Rx3ClockReader::sysPCVString = "SYS / PCVS APPLIED";
const string Rx3ClockReader::numDataString = "# / TYPES OF DATA";
const string Rx3ClockReader::stationNameString = "STATION NAME / NUM";
const string Rx3ClockReader::stationClockRefString = "STATION CLK REF";
const string Rx3ClockReader::analysisCenterString = "ANALYSIS CENTER";
const string Rx3ClockReader::numClockRefString = "# OF CLK REF";
const string Rx3ClockReader::analysisClkRefrString = "ANALYSIS CLK REF";
const string Rx3ClockReader::numReceiversString = "# OF SOLN STA / TRF";
const string Rx3ClockReader::solnStateString = "SOLN STA NAME / NUM";
const string Rx3ClockReader::numSolnSatsString = "# OF SOLN SATS";
const string Rx3ClockReader::prnListString = "PRN LIST";
const string Rx3ClockReader::endOfHeaderString = "END OF HEADER";


void Rx3ClockReader::readHeader(std::fstream &strm) {


    string line;
    while (true) {

        getline(strm, line);
        stripTrailing(line);

        if (debug)
            cout << "Rinex3Clock Header Line " << line << endl;


        // first let's read the rinex clock version
        if (firstLine) {
            version = safeStod(strip(line.substr(0, 9)));
            firstLine = false;
        }

        if (debug) {
            cout << "version:" << version << endl;
        }
        if (version < 3.00) {
            FFStreamError e("Rinex clock version MUST be greater and equal than 3.00");
            throw (e);
        }


        string label;
        if (version >= 3.04) {
            label = strip(line.substr(65, 20));
        } else {
            label = strip(line.substr(60, 20));
        }

        if (label == endOfHeaderString)
            break;

        if (debug) {
            cout << label << endl;
        }

        if (label == versionString) {
            if (version >= 3.04) {
                if (line[21] != 'C') {
                    FFStreamError e("Invalid file type: " + line.substr(21, 1));
                    throw (e);
                }
            } else {
                if (line[20] != 'C') {
                    FFStreamError e("Invalid file type: " + line.substr(20, 1));
                    throw (e);
                }
            }

        } else if (label == runByString) {
            if (version >= 3.04) {
                program = strip(line.substr(0, 21));
                runby = strip(line.substr(21, 21));
            } else {
                program = strip(line.substr(0, 20));
                runby = strip(line.substr(20, 20));
            }

        } else if (label == intervalString) {
            interval = safeStod(strip(line.substr(0, 20)));
        } else if (label == commentString) {
            if (version >= 3.04) {
                commentList.push_back(strip(line.substr(0, 66)));
            } else {
                commentList.push_back(strip(line.substr(0, 60)));
            }
        } else if (label == timeSystemString) {
            string ts((line.substr(3, 3)));
            timeSystem = TimeSystem(ts);
        } else if (label == leapSecondsString) {
            leapSeconds = safeStoi(line.substr(0, 6));
        } else if (label == leapSecondsGNSSString) {
            leapSeconds = safeStoi(line.substr(0, 6));
        } else if (label == numDataString) {
            int n(safeStoi(line.substr(0, 6)));
            for (int i = 0; i < n; ++i)
                dataTypes.push_back(line.substr(10 + i * 6, 2));
        }

    }  // end while end-of-header not found

}

void Rx3ClockReader::readData(std::fstream &strm) {

    while (true) {
        string line;
        getline(strm, line);
        if (strm.eof()) {
            break;
        }

        stripTrailing(line);
        if (line.length() < 59) {
            FFStreamError e("Short line : " + line);
            throw (e);
        }

        if (debug)
            cout << "Data Line: /" << line << "/" << endl;

        datatype = line.substr(0, 2);
        site = line.substr(3, 4);
        if (datatype == string("AS")) {
            strip(site);
            sat = SatID(site);

            CivilTime time;
            CommonTime currentEpoch;

            ///////////////////
            if (version >= 3.04) {
                time = CivilTime(safeStoi(line.substr(8 + 5, 4)),
                                 safeStoi(line.substr(12 + 5, 3)),
                                 safeStoi(line.substr(15 + 5, 3)),
                                 safeStoi(line.substr(18 + 5, 3)),
                                 safeStoi(line.substr(21 + 5, 3)),
                                 safeStod(line.substr(24 + 5, 10)),
                                 TimeSystem::GPS);

                currentEpoch = CivilTime2CommonTime(time);

                if (debug) {
                    cout << CommonTime2YDSTime(currentEpoch) << endl;
                }

                int n(safeStoi(line.substr(34 + 5, 3)));
                bias = safeStod(line.substr(40 + 5, 19));

                if (debug) {
                    cout << setiosflags(ios::fixed)
                         << setprecision(19) << "bias:" << bias << endl;
                }

                if (n > 1 && line.length() >= 59 + 5)
                    sig_bias = safeStod(line.substr(60 + 5, 19));

                if (n > 2) {
                    getline(strm, line);
                    if (strm.eof()) {
                        EndOfFile e("end of file");
                        throw (e);
                    }

                    stripTrailing(line);
                    if (int(line.length()) < (n - 2) * 20 - 1) {
                        FFStreamError e("Short line : " + line);
                        throw (e);
                    }

                    drift = safeStod(line.substr(0, 19));
                    if (n > 3)
                        sig_drift = safeStod(line.substr(20, 19));
                    if (n > 4)
                        accel = safeStod(line.substr(40, 19));
                    if (n > 5)
                        sig_accel = safeStod(line.substr(60, 19));
                }
            } else {

                time = CivilTime(safeStoi(line.substr(8, 4)),
                                 safeStoi(line.substr(12, 3)),
                                 safeStoi(line.substr(15, 3)),
                                 safeStoi(line.substr(18, 3)),
                                 safeStoi(line.substr(21, 3)),
                                 safeStod(line.substr(24, 10)),
                                 TimeSystem::GPS);

                currentEpoch = CivilTime2CommonTime(time);

                int n(safeStoi(line.substr(34, 3)));
                bias = safeStod(line.substr(40, 19));
                if (n > 1 && line.length() >= 59)
                    sig_bias = safeStod(line.substr(60, 19));

                if (n > 2) {
                    getline(strm, line);
                    if (strm.eof()) {
                        EndOfFile e("end of file");
                        throw (e);
                    }

                    stripTrailing(line);
                    if (int(line.length()) < (n - 2) * 20 - 1) {
                        FFStreamError e("Short line : " + line);
                        throw (e);
                    }

                    drift = safeStod(line.substr(0, 19));
                    if (n > 3)
                        sig_drift = safeStod(line.substr(20, 19));
                    if (n > 4)
                        accel = safeStod(line.substr(40, 19));
                    if (n > 5)
                        sig_accel = safeStod(line.substr(60, 19));
                }

            }


            if (debug) {
                cout << CommonTime2CivilTime(currentEpoch) << " ::bias:" << bias << endl;
            }

            // store data into satClockData;
            satClockData[sat][currentEpoch] = bias;

        }
    }
}



//------------------------------------------------------------------------------------
