#include "SinexBiasStore.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "Const.h"
#include "StringUtils.h"
#include "TimeConvert.h"

namespace {

std::vector<std::string> splitWhitespace(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           lowerCopy(s.substr(s.size() - suffix.size())) == suffix;
}

// "YYYY:DOY:SOD" - the epoch spelling used throughout Bias-SINEX.
bool looksLikeEpoch(const std::string &tok) {
    if (tok.size() < 9) return false;
    const std::size_t p1 = tok.find(':');
    if (p1 == std::string::npos || p1 < 4) return false;
    const std::size_t p2 = tok.find(':', p1 + 1);
    if (p2 == std::string::npos || p2 == p1 + 1) return false;
    for (std::size_t i = 0; i < p1; ++i)
        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
    for (std::size_t i = p1 + 1; i < p2; ++i)
        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
    if (p2 + 1 >= tok.size()) return false;
    for (std::size_t i = p2 + 1; i < tok.size(); ++i) {
        const char c = tok[i];
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return false;
    }
    return true;
}

bool looksLikePrn(const std::string &prn) {
    if (prn.size() != 3) return false;
    if (!std::isalpha(static_cast<unsigned char>(prn[0]))) return false;
    return std::isdigit(static_cast<unsigned char>(prn[1])) &&
           std::isdigit(static_cast<unsigned char>(prn[2]));
}

double strictDouble(const std::string &tok) {
    std::size_t used = 0;
    const double v = std::stod(tok, &used);
    if (used != tok.size()) throw std::invalid_argument("trailing characters");
    return v;
}

} // namespace

void SinexBiasStore::clear() {
    osbData.clear();
    codeObsTypes.clear();
    version.clear();
    biasMode.clear();
    timeSystemCode.clear();
    biasTimeSystem = TimeSystem(TimeSystem::GPS);
    clockReferences.clear();
    headerRecordCount = 0;
    totalBiasRecords = 0;
    codeIntervalCount = 0;
    phaseRecordCount = 0;
    dsbRecordCount = 0;
    stationRecordCount = 0;
    filenameForErrors.clear();
}

void SinexBiasStore::loadFile(const std::string &filename) {
    clear();
    filenameForErrors = filename;

    if (endsWith(filename, ".gz") || endsWith(filename, ".z") ||
        endsWith(filename, ".zip")) {
        FFStreamError e("Bias file " + filename +
                        " is compressed; gunzip it first");
        throw(e);
    }

    std::fstream strm(filename);
    if (!strm) {
        FileMissingException e("File " + filename + " could not be opened");
        throw(e);
    }

    readHeader(strm);
    readData(strm);
    strm.close();

    if (codeIntervalCount == 0) {
        FFStreamError e("Bias file " + filename + " carries no usable code OSB records");
        throw(e);
    }
}

void SinexBiasStore::readHeader(std::fstream &strm) {
    std::string line;
    bool sawVersion = false;
    bool sawSolution = false;
    bool inDescription = false;

    while (std::getline(strm, line)) {
        stripTrailing(line);
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        const std::string s = strip(line);
        if (s.empty()) continue;

        if (s.rfind("%=BIA", 0) == 0) {
            const std::vector<std::string> tok = splitWhitespace(s);
            if (tok.size() >= 2) version = tok[1];
            if (!tok.empty()) {
                const std::string &last = tok.back();
                const bool digits = std::all_of(
                        last.begin(), last.end(),
                        [](unsigned char c) { return std::isdigit(c); });
                if (digits) headerRecordCount = std::stoul(last);
            }
            sawVersion = true;
            continue;
        }
        if (s == "+BIAS/DESCRIPTION") { inDescription = true; continue; }
        if (s == "-BIAS/DESCRIPTION") { inDescription = false; continue; }
        if (s == "+BIAS/SOLUTION") { sawSolution = true; break; }

        if (inDescription) parseDescriptionLine(s);
    }

    if (!sawVersion) {
        FFStreamError e("File " + filenameForErrors +
                        " does not start with a %=BIA header");
        throw(e);
    }
    if (version != "1.00") {
        FFStreamError e("File " + filenameForErrors + " is Bias-SINEX version " +
                        version + "; only 1.00 is supported");
        throw(e);
    }
    if (!sawSolution) {
        FFStreamError e("File " + filenameForErrors + " has no +BIAS/SOLUTION block");
        throw(e);
    }
    if (biasMode.empty()) {
        FFStreamError e("File " + filenameForErrors + " does not declare BIAS_MODE");
        throw(e);
    }
    if (biasMode != "ABSOLUTE") {
        FFStreamError e("File " + filenameForErrors + " has BIAS_MODE " + biasMode +
                        "; only ABSOLUTE biases can be applied to observations");
        throw(e);
    }
    if (timeSystemCode.empty()) {
        FFStreamError e("File " + filenameForErrors + " does not declare TIME_SYSTEM");
        throw(e);
    }
}

void SinexBiasStore::parseDescriptionLine(const std::string &line) {
    const std::vector<std::string> tok = splitWhitespace(line);
    if (tok.empty()) return;

    if (tok[0] == "BIAS_MODE" && tok.size() >= 2) {
        biasMode = tok[1];
        return;
    }
    if (tok[0] == "TIME_SYSTEM" && tok.size() >= 2) {
        timeSystemCode = tok[1];
        const char c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(timeSystemCode[0])));
        TimeSystem::SystemType sys = TimeSystem::Unknown;
        switch (c) {
            case 'G': sys = TimeSystem::GPS; break;
            case 'C': sys = TimeSystem::BDT; break;
            case 'B': sys = TimeSystem::BDT; break;
            case 'E': sys = TimeSystem::GAL; break;
            case 'R': sys = TimeSystem::GLO; break;
            case 'J': sys = TimeSystem::QZS; break;
            case 'U': sys = TimeSystem::UTC; break;
            default: break;
        }
        if (sys == TimeSystem::Unknown) {
            FFStreamError e("File " + filenameForErrors +
                            " declares unsupported TIME_SYSTEM " + timeSystemCode);
            throw(e);
        }
        biasTimeSystem = TimeSystem(sys);
        return;
    }
    if (tok[0].rfind("SATELLITE_CLOCK_REFERENCE_OBSERVABLES", 0) == 0 &&
        tok.size() >= 2) {
        std::vector<std::string> types;
        for (std::size_t i = 2; i < tok.size(); ++i) types.push_back(tok[i]);
        clockReferences[tok[1]] = types;
    }
}

void SinexBiasStore::readData(std::fstream &strm) {
    std::string line;
    std::size_t lineNo = 0;
    bool sawEnd = false;

    while (std::getline(strm, line)) {
        ++lineNo;
        stripTrailing(line);
        const std::string s = strip(line);
        if (s.empty()) continue;
        if (s == "-BIAS/SOLUTION") { sawEnd = true; break; }
        if (s[0] == '*') continue;
        // Another block's label; only +BIAS/SOLUTION records are ours.
        if (s[0] == '+' || s[0] == '-') continue;
        parseRecord(line, lineNo);
    }

    if (!sawEnd) {
        // EOF without the closing label is tolerated only if records were read;
        // the record content, not the trailer, is what the caller needs.
        if (codeIntervalCount == 0) {
            FFStreamError e("File " + filenameForErrors +
                            " ends inside +BIAS/SOLUTION without usable OSB records");
            throw(e);
        }
    }
}

void SinexBiasStore::parseRecord(const std::string &line, std::size_t lineNo) {
    std::string type;
    if (line.size() >= 4) type = strip(line.substr(0, 4));
    if (type.empty()) type = strip(line);
    if (type != "OSB" && type != "DSB") {
        const std::vector<std::string> tok = splitWhitespace(strip(line));
        if (tok.empty()) return;
        type = tok[0];
    }
    if (type == "DSB") {
        ++totalBiasRecords;
        ++dsbRecordCount;
        return;
    }
    if (type != "OSB") return;   // unknown record type: ignore

    ++totalBiasRecords;
    bool ok = false;
    parseRecordFixedWidth(line, lineNo, ok);
    if (!ok) parseRecordTokens(line, lineNo);
}

void SinexBiasStore::parseRecordFixedWidth(const std::string &line,
                                           std::size_t lineNo, bool &ok) {
    ok = false;
    if (line.size() < 67) return;

    const std::string prn     = strip(line.substr(10, 4));
    const std::string station = strip(line.substr(14, 11));
    const std::string obs1    = strip(line.substr(25, 3));
    const std::string obs2    = strip(line.substr(28, 7));
    const std::string start   = strip(line.substr(35, 15));
    const std::string end     = strip(line.substr(50, 15));
    const std::string unit    = strip(line.substr(65, 4));
    // The value and the standard deviation share the tail. Take the first token
    // rather than a fixed width: the field is 21 columns in the products seen
    // so far, but scientific notation may grow past it.
    const std::vector<std::string> tail = splitWhitespace(line.substr(69));
    const std::string value = tail.empty() ? std::string() : tail[0];

    if (obs1.size() != 3 || !looksLikeEpoch(start) || !looksLikeEpoch(end) ||
        unit.empty() || value.empty() || !looksLikePrn(prn)) {
        return;
    }

    storeRecord("OSB", prn, station, obs1, obs2, start, end, unit, value, lineNo);
    ok = true;
}

void SinexBiasStore::parseRecordTokens(const std::string &line,
                                       std::size_t lineNo) {
    const std::vector<std::string> tok = splitWhitespace(strip(line));
    if (tok.size() < 5 || tok[0] != "OSB") {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed OSB record");
        throw(e);
    }

    std::size_t d = 0;
    for (; d < tok.size(); ++d)
        if (looksLikeEpoch(tok[d])) break;
    if (d + 3 >= tok.size() || d < 4) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": cannot locate the epochs");
        throw(e);
    }

    const std::string prn = tok[2];
    std::string station, obs1;
    if (d - 3 == 1) {
        obs1 = tok[3];
    } else if (d - 3 == 2) {
        station = tok[3];
        obs1 = tok[4];
    } else {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": unexpected field count");
        throw(e);
    }

    storeRecord("OSB", prn, station, obs1, "", tok[d], tok[d + 1], tok[d + 2],
                tok[d + 3], lineNo);
}

void SinexBiasStore::storeRecord(const std::string &type,
                                 const std::string &prn,
                                 const std::string &station,
                                 const std::string &obs1,
                                 const std::string &obs2,
                                 const std::string &startTok,
                                 const std::string &endTok,
                                 const std::string &unit,
                                 const std::string &valueTok,
                                 std::size_t lineNo) {
    if (type != "OSB") return;
    if (!obs2.empty()) { ++dsbRecordCount; return; }
    if (!station.empty()) { ++stationRecordCount; return; }
    if (obs1.size() != 3) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed observable '" +
                        obs1 + "'");
        throw(e);
    }

    const char kind = static_cast<char>(
            std::toupper(static_cast<unsigned char>(obs1[0])));
    if (kind == 'L') { ++phaseRecordCount; return; }
    if (kind != 'C') return;   // code only; Doppler/SNR are not used

    if (!looksLikePrn(prn)) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed PRN '" + prn + "'");
        throw(e);
    }

    SatID sat;
    try {
        sat = SatID(prn);
    } catch (const std::exception &) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed PRN '" + prn + "'");
        throw(e);
    }

    const CommonTime start = parseEpoch(startTok, lineNo);
    const CommonTime end = parseEpoch(endTok, lineNo);
    if (!(start < end)) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) +
                        ": BIAS_START is not before BIAS_END");
        throw(e);
    }

    const double metres = parseValueMetres(unit, valueTok, lineNo);
    addInterval(sat, obs1, start, end, metres);
}

CommonTime SinexBiasStore::parseEpoch(const std::string &token,
                                      std::size_t lineNo) const {
    const std::size_t p1 = token.find(':');
    const std::size_t p2 = (p1 == std::string::npos)
                           ? std::string::npos : token.find(':', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed epoch '" + token + "'");
        throw(e);
    }

    int year = 0, doy = 0;
    double sod = 0.0;
    try {
        year = std::stoi(token.substr(0, p1));
        doy = std::stoi(token.substr(p1 + 1, p2 - p1 - 1));
        sod = strictDouble(token.substr(p2 + 1));
    } catch (const std::exception &) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed epoch '" + token + "'");
        throw(e);
    }
    if (year < 1980 || year > 2100 || doy < 1 || doy > 366 ||
        sod < 0.0 || sod > static_cast<double>(SEC_PER_DAY)) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": epoch out of range '" +
                        token + "'");
        throw(e);
    }

    // NB: YDSTime2CommonTime() would be the obvious helper, but it stores the
    // Julian Date where CommonTime expects MJD (src/TimeConvert.cpp). Build the
    // MJD here instead and let CommonTime normalize 86400 into the next day.
    const double jan1jd = convertYMD2JD(year, 1, 1);
    const long jan1mjd = static_cast<long>(std::llround(jan1jd - MJD_TO_JD));
    CommonTime ct(jan1mjd + (doy - 1), sod, biasTimeSystem);
    ct.normalize();
    return ct;
}

double SinexBiasStore::parseValueMetres(const std::string &unit,
                                        const std::string &valueTok,
                                        std::size_t lineNo) const {
    double value = 0.0;
    try {
        value = strictDouble(valueTok);
    } catch (const std::exception &) {
        FFStreamError e("File " + filenameForErrors + ", record line " +
                        std::to_string(lineNo) + ": malformed bias value '" +
                        valueTok + "'");
        throw(e);
    }

    const std::string u = lowerCopy(strip(unit));
    if (u == "ns") return value * 1.e-9 * C_MPS;
    if (u == "s")  return value * C_MPS;
    if (u == "m")  return value;

    FFStreamError e("File " + filenameForErrors + ", record line " +
                    std::to_string(lineNo) + ": unsupported bias unit '" + unit +
                    "' (expected ns, s or m)");
    throw(e);
}

void SinexBiasStore::addInterval(const SatID &sat,
                                 const std::string &obsType3,
                                 const CommonTime &start,
                                 const CommonTime &end,
                                 double metres) {
    std::vector<OsbInterval> &v = osbData[sat][obsType3];
    v.push_back(OsbInterval{start, end, metres});
    std::stable_sort(v.begin(), v.end(),
                     [](const OsbInterval &a, const OsbInterval &b) {
                         return a.start < b.start;
                     });
    codeObsTypes[sat.system].insert(obsType3);
    ++codeIntervalCount;
}

bool SinexBiasStore::getOsb(const SatID &sat,
                            const std::string &obsType3,
                            const CommonTime &time,
                            double &metres) const {
    const auto satIt = osbData.find(sat);
    if (satIt == osbData.end()) return false;
    const auto obsIt = satIt->second.find(obsType3);
    if (obsIt == satIt->second.end()) return false;

    CommonTime tq = time;
    long day = 0;
    double sod = 0.0;
    TimeSystem ts;
    tq.get(day, sod, ts);
    if (ts != biasTimeSystem) {
        try {
            tq = convertTimeSystem(tq, biasTimeSystem);
        } catch (const std::exception &) {
            return false;
        }
    }

    const std::vector<OsbInterval> &v = obsIt->second;
    // First interval whose start is strictly after tq; everything before it is
    // a candidate, latest first. Overlaps (which the format permits) resolve to
    // the interval with the latest start.
    auto it = std::upper_bound(
            v.begin(), v.end(), tq,
            [](const CommonTime &t, const OsbInterval &iv) {
                return t < iv.start;
            });
    while (it != v.begin()) {
        --it;
        if (!(tq < it->start) && tq < it->end) {
            metres = it->valueMetres;
            return true;
        }
    }
    return false;
}

const std::vector<SinexBiasStore::OsbInterval> *SinexBiasStore::getIntervals(
        const SatID &sat, const std::string &obsType3) const {
    const auto satIt = osbData.find(sat);
    if (satIt == osbData.end()) return nullptr;
    const auto obsIt = satIt->second.find(obsType3);
    if (obsIt == satIt->second.end()) return nullptr;
    return &obsIt->second;
}

bool SinexBiasStore::hasObsType(const std::string &system,
                                const std::string &obsType3) const {
    const auto it = codeObsTypes.find(system);
    return it != codeObsTypes.end() && it->second.count(obsType3) > 0;
}
