/**
 * Inspect a Bias-SINEX (SINEX-BIAS / OSB) file.
 *
 * This is the command-line face of SinexBiasStore: it prints what the reader
 * kept and what it skipped, dumps the validity intervals of one satellite and
 * observable, and - with --raw - pushes a synthetic observation through
 * SPPIFCode::correctCodeBias so the sign and the fail-closed rule are visible
 * without a downloaded product.
 *
 *   sinex_bias --osb <file.bia>
 *   sinex_bias --osb <file.bia> --sat C06 --obs C2I
 *   sinex_bias --osb <file.bia> --sat C06 --obs C2I --epoch 2022:062:10000
 *   sinex_bias --osb <file.bia> --sat C06 --obs C2I --epoch 2022:062:10000 \
 *              --raw 100.0
 *
 * Epochs are the file's own YYYY:DOY:SOD spelling, in GPS time.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Const.h"
#include "GnssStruct.h"
#include "SPPIFCode.h"
#include "SinexBiasStore.hpp"
#include "TimeConvert.h"

using namespace std;

namespace {

void printUsage(const char *prog) {
    cout <<
         "Usage: " << prog << " --osb <file.bia> [options]\n"
         "\n"
         "Inspect a Bias-SINEX/OSB file.\n"
         "\n"
         "  --osb <file>        Bias-SINEX file (required)\n"
         "  --sat <PRN>         Satellite, e.g. C06 (with --obs)\n"
         "  --obs <OBS>         Code observable, e.g. C2I, C5P (with --sat)\n"
         "  --epoch YYYY:DOY:SOD\n"
         "                      Query one epoch, e.g. 2022:062:10000\n"
         "  --raw <metres>      Feed a synthetic code observation of this value\n"
         "                      through SPPIFCode::correctCodeBias; requires\n"
         "                      --epoch, --sat and --obs\n"
         "  -h, --help          This message\n";
}

bool parseYds(const string &s, CommonTime &out) {
    int year = 0, doy = 0;
    double sod = 0.0;
    if (std::sscanf(s.c_str(), "%d:%d:%lf", &year, &doy, &sod) != 3)
        return false;
    if (year < 1980 || year > 2100 || doy < 1 || doy > 366 ||
        sod < 0.0 || sod > static_cast<double>(SEC_PER_DAY))
        return false;

    // Do not use YDSTime2CommonTime(): it stores a Julian Date where
    // CommonTime expects an MJD. Build the MJD directly (see the same note in
    // SinexBiasStore::parseEpoch).
    const double jan1jd = convertYMD2JD(year, 1, 1);
    const long jan1mjd = static_cast<long>(std::llround(jan1jd - MJD_TO_JD));
    out = CommonTime(jan1mjd + (doy - 1), sod, TimeSystem(TimeSystem::GPS));
    out.normalize();
    return true;
}

string formatYds(const CommonTime &ct) {
    const YDSTime ydst = CommonTime2YDSTime(ct);
    ostringstream oss;
    oss << setfill('0') << setw(4) << ydst.year << ":"
        << setw(3) << ydst.doy << ":"
        << setw(5) << fixed << setprecision(0) << ydst.sod;
    return oss.str();
}

double metresToNs(double m) {
    return m / (1.e-9 * C_MPS);
}

int fail(const string &message, int code = 1) {
    cerr << "Error: " << message << "\n";
    return code;
}

} // namespace

int main(int argc, char **argv) {
    string osbFile, satArg, obsArg, epochArg;
    double rawValue = 0.0;
    bool haveRaw = false;

    for (int i = 1; i < argc; ++i) {
        const string a = argv[i];
        auto needValue = [&](const char *name) -> string {
            if (i + 1 >= argc) {
                cerr << "Error: " << name << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else if (a == "--osb") osbFile = needValue("--osb");
        else if (a == "--sat") satArg = needValue("--sat");
        else if (a == "--obs") obsArg = needValue("--obs");
        else if (a == "--epoch") epochArg = needValue("--epoch");
        else if (a == "--raw") {
            const string v = needValue("--raw");
            try {
                rawValue = std::stod(v);
            } catch (const std::exception &) {
                return fail("--raw needs a number, got '" + v + "'", 2);
            }
            haveRaw = true;
        } else {
            cerr << "Error: unknown option '" << a << "'\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    if (osbFile.empty()) {
        cerr << "Error: --osb <file> is required\n";
        printUsage(argv[0]);
        return 2;
    }
    if (satArg.empty() != obsArg.empty()) {
        return fail("--sat and --obs must be given together", 2);
    }
    if (!epochArg.empty() && (satArg.empty() || obsArg.empty())) {
        return fail("--epoch requires --sat and --obs", 2);
    }
    if (haveRaw && epochArg.empty()) {
        return fail("--raw requires --sat, --obs and --epoch", 2);
    }
    if (!obsArg.empty() &&
        (obsArg.size() != 3 || obsArg[0] != 'C')) {
        return fail("--obs must be a three-character code observable, e.g. C2I", 2);
    }

    SinexBiasStore store;
    try {
        store.loadFile(osbFile);
    } catch (const std::exception &e) {
        return fail(e.what());
    }

    //---------------------------------------------------------------
    // Summary
    //---------------------------------------------------------------
    cout << "file          : " << osbFile << "\n";
    cout << "version       : " << store.getVersion() << "\n";
    cout << "bias mode     : " << store.getBiasMode() << "\n";
    cout << "time system   : " << store.getTimeSystemCode()
         << " (" << store.getTimeSystem().toString() << ")\n";
    cout << "records       : " << store.getTotalBiasRecords()
         << " bias records, " << store.getDsbRecordCount()
         << " DSB skipped, " << store.getStationRecordCount()
         << " station-specific skipped\n";
    cout << "                code intervals " << store.getCodeIntervalCount()
         << ", phase skipped " << store.getPhaseRecordCount() << "\n";
    if (store.getHeaderRecordCount() > 0) {
        cout << "header count  : " << store.getHeaderRecordCount()
             << (store.getHeaderRecordCount() == store.getTotalBiasRecords()
                 ? " (matches)" : " (does not match the records parsed)")
             << "\n";
    }
    cout << "satellites    : " << store.getSatelliteCount() << "\n";

    for (const auto &kv : store.getClockReferences()) {
        cout << "clock ref " << kv.first << "   :";
        for (const string &t : kv.second) cout << " " << t;
        cout << "\n";
    }

    cout << "code coverage :\n";
    for (const auto &kv : store.getCodeObsTypes()) {
        cout << "  " << kv.first << " :";
        for (const string &t : kv.second) cout << " " << t;
        cout << "\n";
    }

    //---------------------------------------------------------------
    // Query
    //---------------------------------------------------------------
    if (satArg.empty()) return 0;

    SatID sat;
    try {
        sat = SatID(satArg);
    } catch (const std::exception &) {
        return fail("malformed satellite '" + satArg + "'", 2);
    }

    const vector<SinexBiasStore::OsbInterval> *intervals =
            store.getIntervals(sat, obsArg);
    if (intervals == nullptr) {
        return fail("no OSB records for " + sat.toString() + " " + obsArg);
    }

    cout << "intervals     :\n";
    for (const auto &iv : *intervals) {
        cout << "  " << sat.toString() << " " << obsArg << " "
             << formatYds(iv.start) << " " << formatYds(iv.end) << " "
             << fixed << setprecision(6) << iv.valueMetres << " m ("
             << setprecision(6) << metresToNs(iv.valueMetres) << " ns)\n";
    }

    if (epochArg.empty()) return 0;

    CommonTime epoch;
    if (!parseYds(epochArg, epoch)) {
        return fail("malformed --epoch '" + epochArg +
                    "' (expected YYYY:DOY:SOD)", 2);
    }

    double bias = 0.0;
    if (!store.getOsb(sat, obsArg, epoch, bias)) {
        cout << "at " << formatYds(epoch) << " : no OSB for "
             << sat.toString() << " " << obsArg << "\n";
        return 1;
    }
    cout << "at " << formatYds(epoch) << " : " << sat.toString() << " " << obsArg
         << " " << fixed << setprecision(6) << bias << " m ("
         << setprecision(6) << metresToNs(bias) << " ns)\n";

    if (!haveRaw) return 0;

    //---------------------------------------------------------------
    // End-to-end sign test: push one observation through the solver hook.
    //---------------------------------------------------------------
    ObsData obsData;
    obsData.epoch = epoch;
    obsData.satTypeValueData[sat][obsArg] = rawValue;

    SPPIFCode solver;
    solver.setBiasStore(&store);
    solver.correctCodeBias(obsData);

    const auto it = obsData.satTypeValueData.find(sat);
    const bool found = it != obsData.satTypeValueData.end() &&
                       it->second.count(obsArg) > 0;
    const double corrected = found ? it->second.at(obsArg) : rawValue;
    cout << "raw " << fixed << setprecision(6) << rawValue
         << " m -> corrected " << setprecision(9) << corrected << " m\n"
         << "corrections applied : " << solver.getCodeBiasCorrections()
         << ", missing (left uncorrected) : " << solver.getCodeBiasMissing()
         << "\n";
    return 0;
}
