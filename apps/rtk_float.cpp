/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * RTK: single-epoch least-squares FLOAT solution.
 *
 * Textbook chapter 8, exercise 1 ("撰写基于最小二乘的 RTK 定位浮点解，并比较 GPS RTK
 * 定位和 BDS RTK 定位精度"). Each epoch is solved independently: both receivers are
 * linearized at their own approximate position with SPPUCCodePhase, the two
 * equation systems are differenced between stations and then between satellites,
 * and the resulting double-difference system is solved by ordinary least squares.
 * Ambiguities stay real-valued - nothing is fixed to an integer, and nothing is
 * carried across epochs.
 *
 * ## What the float solution can and cannot be
 *
 * In this formulation each carrier-phase equation carries its OWN free ambiguity
 * parameter, so for any position the ambiguities can absorb the phase residuals
 * exactly. Eliminating them leaves a zero Schur complement for the coordinates:
 * **the phase observations contribute no information about position at all**, and
 * the float solution's accuracy is that of the pseudorange double differences.
 * That is not a defect of this program - it is what "float, single epoch" means,
 * and it is why the textbook continues to Kalman filtering (which ties the
 * ambiguities across epochs) and to LAMBDA (which makes them integers). Measured
 * here: removing every phase equation leaves the output bit-identical.
 *
 * ## Outputs
 *
 *   <rover>_<sys>_rtk_float.out   one line per epoch, the textbook's format
 *   <rover>_<sys>_rtk_diag.csv    per-epoch conditioning and post-fit diagnostics
 *   <rover>_manifest.json         what produced the above
 *
 * Usage:
 *   rtk_float [config.ini] [options]
 *
 *   config.ini            Configuration file (default: config/rtk.ini).
 *                         Relative paths inside it resolve against the config
 *                         file's own directory.
 *
 * Options (override the config file):
 *   --obs <file>          Rover observation file
 *   --base-obs <file>     Base observation file
 *   --nav <file>          Broadcast navigation file
 *   --out-dir <dir>       Output directory
 *   --stop <ISO8601>      Stop epoch, e.g. 2022-03-03T06:49:00
 *   --sys <mode>          gps | bds2 | bds3  (default: gps)
 *   --dump-epoch <sod>    Print the double-difference system for one epoch
 *   --verbose             Per-epoch progress on stdout
 *   -h, --help            This message
 */

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <limits>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <system_error>
#include <filesystem>

#include <Eigen/Dense>

#include "GnssStruct.h"
#include "CoordConvert.h"
#include "CoordStruct.h"
#include "TimeConvert.h"
#include "GnssFunc.h"
#include "RinexNavStore.hpp"
#include "RinexObsReader.h"
#include "SPPUCCodePhase.h"
#include "SolverLSQ.h"
#include "ConfigData.h"
#include "ConfigReader.h"

#include "app_utils.h"

using namespace std;
using namespace Eigen;

namespace {

void printUsage(const char *prog) {
    cout <<
         "Usage: " << prog << " [config.ini] [options]\n"
         "\n"
         "RTK single-epoch least-squares float solution.\n"
         "\n"
         "  config.ini         Configuration file (default: config/rtk.ini)\n"
         "\n"
         "Options:\n"
         "  --obs <file>       Rover observation file\n"
         "  --base-obs <file>  Base observation file\n"
         "  --nav <file>       Broadcast navigation file\n"
         "  --out-dir <dir>    Output directory\n"
         "  --stop <ISO8601>   Stop epoch, e.g. 2022-03-03T06:49:00\n"
         "  --sys <mode>       Constellation and frequency pair:\n";
    for (const RtkMode &m : rtkModes())
        cout << "                       " << left << setw(6) << m.key << " " << m.label << "\n";
    cout <<
         "  --dump-epoch <sod> Print the double-difference system for one epoch\n"
         "  --verbose          Per-epoch progress\n"
         "  -h, --help         This message\n";
}

/// Per-epoch conditioning and goodness-of-fit numbers for the DD system.
struct DdDiag {
    int nObs = 0;
    int nUnk = 0;
    int rank = 0;
    double cond = 0.0;
    double sigma0 = 0.0;
    double postfitRms = 0.0;
    bool stateUsable = false;
};

/// Rebuild the dense double-difference design matrix and report on it.
///
/// Nothing in the library checks the fit - SolverLSQ::solve computes the state
/// and stops - so a garbage epoch looks exactly like a good one from the output
/// file alone. These numbers are what make the difference visible.
DdDiag diagnoseDd(const EquSys &equSys, const SolverLSQ &solver) {
    DdDiag d;
    d.nObs = (int) equSys.obsEquData.size();
    d.nUnk = (int) equSys.varSet.size();
    if (d.nObs == 0 || d.nUnk == 0) return d;

    MatrixXd H = MatrixXd::Zero(d.nObs, d.nUnk);
    VectorXd pre = VectorXd::Zero(d.nObs);
    VectorXd wgt = VectorXd::Zero(d.nObs);

    int i = 0;
    for (const auto &ed : equSys.obsEquData) {
        pre(i) = ed.second.prefit;
        wgt(i) = ed.second.weight;
        for (const auto &vc : ed.second.varCoeffData) {
            auto it = equSys.varSet.find(vc.first);
            if (it == equSys.varSet.end()) continue;
            H(i, (int) std::distance(equSys.varSet.begin(), it)) = vc.second;
        }
        ++i;
    }

    FullPivLU<MatrixXd> lu(H);
    d.rank = (int) lu.rank();

    JacobiSVD<MatrixXd> svd(H);
    if (svd.singularValues().size() > 0) {
        double smax = svd.singularValues()(0);
        double smin = svd.singularValues()(svd.singularValues().size() - 1);
        d.cond = (smin > 0.0) ? smax / smin : std::numeric_limits<double>::infinity();
    }

    const VectorXd &x = solver.getState();
    if (x.size() == d.nUnk) {
        d.stateUsable = true;
        VectorXd v = H * x - pre;
        double wvv = 0.0;
        for (int k = 0; k < d.nObs; ++k) wvv += wgt(k) * v(k) * v(k);
        int dof = d.nObs - d.nUnk;
        d.sigma0 = (dof > 0) ? std::sqrt(wvv / dof) : 0.0;
        d.postfitRms = std::sqrt(v.squaredNorm() / d.nObs);
    }
    return d;
}

/// Satellite to difference every other satellite against.
///
/// SPPIFCode::getDatumSat() picks the highest-elevation satellite the ROVER can
/// see, with no knowledge of the base. If the base never tracked it, none of its
/// equations survive differenceStation(), differenceSat() then finds no datum
/// equation to subtract and drops every remaining satellite, and the DD system
/// comes back empty. Picking the highest-elevation satellite that actually has
/// equations in the between-station system removes that failure outright; on the
/// reference dataset it selects the same satellite as before.
SatID pickDatumSat(const EquSys &equSysSD, const SatValueMap &satElevData, bool &usedFallback) {
    set<SatID> sdSats;
    for (const auto &oe : equSysSD.obsEquData)
        sdSats.insert(oe.first.sat);

    SatID best;
    double bestElev = -1.0;
    for (const SatID &s : sdSats) {
        auto it = satElevData.find(s);
        double elev = (it == satElevData.end()) ? 0.0 : it->second;
        if (elev > bestElev) {
            bestElev = elev;
            best = s;
        }
    }

    // Report whether this agrees with the rover-only choice, so a change in
    // which satellite anchored an epoch is visible rather than silent.
    SatID roverTop;
    double roverTopElev = -1.0;
    for (const auto &se : satElevData) {
        if (se.second > roverTopElev) {
            roverTopElev = se.second;
            roverTop = se.first;
        }
    }
    usedFallback = !(best == roverTop);
    return best;
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/rtk.ini";
    bool haveConfigArg = false;

    string optObs, optBaseObs, optNav, optOutDir, optStop, optSys;
    bool optVerbose = false;
    double optDumpSod = -1.0;

    for (int i = 1; i < argc; ++i) {
        string a = argv[i];

        auto needValue = [&](const char *name) -> string {
            if (i + 1 >= argc) {
                cerr << "Error: " << name << " requires a value\n";
                exit(2);
            }
            return argv[++i];
        };

        if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else if (a == "--obs")        optObs = needValue("--obs");
        else if (a == "--base-obs")   optBaseObs = needValue("--base-obs");
        else if (a == "--nav")        optNav = needValue("--nav");
        else if (a == "--out-dir")    optOutDir = needValue("--out-dir");
        else if (a == "--stop")       optStop = needValue("--stop");
        else if (a == "--sys")        optSys = needValue("--sys");
        else if (a == "--dump-epoch") optDumpSod = std::stod(needValue("--dump-epoch"));
        else if (a == "--verbose")    optVerbose = true;
        else if (!a.empty() && a[0] == '-') {
            cerr << "Error: unknown option '" << a << "'\n";
            printUsage(argv[0]);
            return 2;
        } else if (!haveConfigArg) {
            configFile = a;
            haveConfigArg = true;
        } else {
            cerr << "Error: unexpected argument '" << a << "'\n";
            return 2;
        }
    }

    //---------------------------------------------------------------
    // Configuration
    //---------------------------------------------------------------
    RTKConfigData cfg = RTKConfigData::defaults();
    string configDir;

    if (!haveConfigArg && !fileExists(configFile)) {
        string found = findConfigUpwards(configFile);
        if (!found.empty()) {
            configFile = found;
            cerr << "Note: using config found by searching upwards: " << configFile << "\n";
        }
    }

    if (!fileExists(configFile)) {
        if (haveConfigArg) {
            cerr << "Error: cannot open config file: " << configFile << "\n";
            return 1;
        }
        cerr << "Note: " << configFile << " not found; using built-in defaults.\n"
             << "      Relative paths then resolve against the working directory ("
             << std::filesystem::current_path().string() << ").\n";
    } else {
        try {
            cfg = RTKConfigData::fromIni(configFile);
            configDir = dirOf(configFile);
        } catch (const std::exception &e) {
            cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    }

    if (!optObs.empty())     cfg.obsFile = optObs;
    if (!optBaseObs.empty()) cfg.baseObsFile = optBaseObs;
    if (!optNav.empty())     cfg.navFile = optNav;
    if (!optOutDir.empty())  cfg.outDir = optOutDir;
    if (!optStop.empty())    cfg.stopUTC = optStop;
    if (!optSys.empty())     cfg.sys = optSys;

    RtkMode mode;
    if (!findRtkMode(cfg.sys, mode)) {
        cerr << "Error: unknown sys '" << cfg.sys << "' (expected one of: "
             << rtkModeKeys() << ")\n";
        return 2;
    }

    string projectRoot = configDir.empty() ? "." : dirOf(configDir);

    string roverFile = optObs.empty()     ? resolvePath(projectRoot, cfg.obsFile)     : cfg.obsFile;
    string baseFile  = optBaseObs.empty() ? resolvePath(projectRoot, cfg.baseObsFile) : cfg.baseObsFile;
    string navFile   = optNav.empty()     ? resolvePath(projectRoot, cfg.navFile)     : cfg.navFile;
    string outDir    = optOutDir.empty()  ? resolvePath(projectRoot, cfg.outDir)      : cfg.outDir;

    if (optVerbose) {
        cout << "config    : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "rover     : " << roverFile << "\n";
        cout << "base      : " << baseFile << "\n";
        cout << "nav       : " << navFile << "\n";
        cout << "outDir    : " << outDir << "\n";
        cout << "sys       : " << mode.key << "  " << mode.label << "\n";
        cout << "stop      : " << (cfg.stopUTC.empty() ? "(end of file)" : cfg.stopUTC) << "\n";
        cout << "cutOff    : " << cfg.cutOffElevation << " deg\n";
    }

    if (!ensureDirectory(outDir)) {
        cerr << "Error: cannot create output directory: " << outDir << "\n";
        return 1;
    }

    //---------------------------------------------------------------
    // Stop epoch
    //---------------------------------------------------------------
    bool haveStop = false;
    CommonTime stopEpoch;
    if (!cfg.stopUTC.empty()) {
        int y, mo, d, h, mi;
        double sec;
        if (!parseISO8601(cfg.stopUTC, y, mo, d, h, mi, sec)) {
            cerr << "Error: malformed stopUTC '" << cfg.stopUTC
                 << "' (expected YYYY-MM-DDTHH:MM:SS)\n";
            return 2;
        }
        stopEpoch = CivilTime2CommonTime(CivilTime(y, mo, d, h, mi, sec));
        haveStop = true;
    }

    //---------------------------------------------------------------
    // Open input
    //---------------------------------------------------------------
    std::fstream roverObsStream(roverFile);
    if (!roverObsStream) {
        cerr << "Error: cannot open rover observation file: " << roverFile << "\n";
        return 1;
    }

    std::fstream baseObsStream(baseFile);
    if (!baseObsStream) {
        cerr << "Error: cannot open base observation file: " << baseFile << "\n";
        return 1;
    }

    RinexNavStore navStore;
    try {
        navStore.loadFile(navFile);
    } catch (const std::exception &e) {
        cerr << "Error: cannot load navigation file: " << navFile << "\n"
             << "       " << e.what() << "\n";
        return 1;
    }

    //---------------------------------------------------------------
    // Observation types and solver setup
    //---------------------------------------------------------------
    // The reader selects on the RAW three-character names; SPPUCCodePhase then
    // keys everything off the collapsed two-character pair. Both come from the
    // same RtkMode so they cannot drift apart.
    std::map<string, std::set<string>> selectedTypes;
    selectedTypes[mode.system] = mode.rawTypes;

    std::map<string, std::pair<string, string>> dualCodeTypes;
    dualCodeTypes[mode.system] = mode.codePair;

    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    RinexObsReader readObsBase;
    readObsBase.setFileStream(&baseObsStream);
    readObsBase.setSelectedTypes(selectedTypes);

    SPPUCCodePhase sppRover;
    sppRover.setRinexNavStore(&navStore);
    sppRover.setDualCodeTypes(dualCodeTypes);
    sppRover.setCutOffElev(cfg.cutOffElevation);

    SPPUCCodePhase sppBase;
    sppBase.setRinexNavStore(&navStore);
    sppBase.setStationAsBase();
    sppBase.setDualCodeTypes(dualCodeTypes);
    sppBase.setCutOffElev(cfg.cutOffElevation);

    SolverLSQ solverRTK;

    //---------------------------------------------------------------
    // Output files
    //---------------------------------------------------------------
    string obsBase = fileNameOf(roverFile);
    string solFile  = outDir + "/" + obsBase + "_" + mode.key + "_rtk_float.out";
    string diagFile = outDir + "/" + obsBase + "_" + mode.key + "_rtk_diag.csv";

    std::fstream solStream(solFile, ios::out);
    if (!solStream) {
        cerr << "Error: cannot open solution file: " << solFile << "\n";
        return 1;
    }

    std::ofstream diagStream(diagFile, ios::trunc);
    if (!diagStream) {
        cerr << "Error: cannot open diagnostics file: " << diagFile << "\n";
        return 1;
    }
    diagStream << "sod,nRoverEq,nSD,nDD,nUnk,rank,cond,datumSat,datumFallback,"
                  "nSDsats,absDxyz,sigma0,postfitRms\n";
    diagStream << fixed << setprecision(6);

    //---------------------------------------------------------------
    // Epoch loop
    //---------------------------------------------------------------
    long epochCount = 0;
    long skipNoRoverSpp = 0, skipSync = 0, skipNoBaseSpp = 0;
    long skipNoSd = 0, skipNoDd = 0, skipDatumFallback = 0;

    while (true) {
        ObsData roverData;
        try {
            roverData = readObsRover.parseRinexObs();
        }
        catch (EndOfFile &) {
            break;
        }

        CommonTime epoch = roverData.epoch;
        YDSTime ydst = CommonTime2YDSTime(epoch);
        double sod = ydst.sod;

        // --- rover: single-point positioning ---------------------------------
        try {
            sppRover.solve(roverData);
        }
        catch (std::exception &e) {
            ++skipNoRoverSpp;
            if (optVerbose)
                cout << "[skip] sod " << sod << " rover SPP: " << e.what() << "\n";
            continue;
        }

        EquSys equSysRover = sppRover.getEquSys();

        // --- base: synchronised single-point positioning ---------------------
        ObsData baseData;
        try {
            baseData = readObsBase.parseRinexObs(epoch);
        }
        catch (SyncException &) {
            // The base file has no epoch matching this one. Not an error: the
            // two receivers can drop different epochs. Move on.
            ++skipSync;
            continue;
        }
        catch (EndOfFile &) {
            break;
        }

        try {
            sppBase.solve(baseData);
        }
        catch (std::exception &e) {
            ++skipNoBaseSpp;
            if (optVerbose)
                cout << "[skip] sod " << sod << " base SPP: " << e.what() << "\n";
            continue;
        }

        EquSys equSysBase = sppBase.getEquSys();

        // --- between stations ------------------------------------------------
        EquSys equSysSD;
        differenceStation(equSysRover, equSysBase, equSysSD);

        set<SatID> sdSats;
        for (const auto &oe : equSysSD.obsEquData)
            sdSats.insert(oe.first.sat);

        if (sdSats.size() < 2) {
            ++skipNoSd;
            if (optVerbose)
                cout << "[skip] sod " << sod << " only " << sdSats.size()
                     << " satellite(s) in common\n";
            continue;
        }

        // --- between satellites ----------------------------------------------
        bool datumFallback = false;
        SatID datumSat = pickDatumSat(equSysSD, sppRover.getSatElevData(), datumFallback);
        if (datumFallback) ++skipDatumFallback;

        EquSys equSysDD;
        differenceSat(datumSat, equSysSD, equSysDD);

        if (equSysDD.obsEquData.empty() || equSysDD.varSet.size() < 3) {
            ++skipNoDd;
            if (optVerbose)
                cout << "[skip] sod " << sod << " empty double-difference system ("
                     << equSysDD.obsEquData.size() << " obs, "
                     << equSysDD.varSet.size() << " unknowns)\n";
            continue;
        }

        // --- solve -----------------------------------------------------------
        try {
            solverRTK.solve(equSysDD);
        }
        catch (std::exception &e) {
            ++skipNoDd;
            if (optVerbose)
                cout << "[skip] sod " << sod << " DD solve: " << e.what() << "\n";
            continue;
        }

        Vector3d dxyzRTK = solverRTK.getdxyz();
        Vector3d xyzRover = sppRover.getXYZ();
        Vector3d xyzRTKFloat = xyzRover + dxyzRTK;

        DdDiag diag = diagnoseDd(equSysDD, solverRTK);

        diagStream << sod << ","
                   << equSysRover.obsEquData.size() << ","
                   << equSysSD.obsEquData.size() << ","
                   << diag.nObs << ","
                   << diag.nUnk << ","
                   << diag.rank << ","
                   << diag.cond << ","
                   << datumSat.toString() << ","
                   << (datumFallback ? 1 : 0) << ","
                   << sdSats.size() << ","
                   << dxyzRTK.norm() << ","
                   << diag.sigma0 << ","
                   << diag.postfitRms << "\n";

        printSolution(solStream, epoch, xyzRover, xyzRTKFloat);
        ++epochCount;

        if (optVerbose)
            cout << "[rtk] sod " << sod
                 << "  dxyz " << dxyzRTK.transpose()
                 << "  n=" << diag.nObs << "/" << diag.nUnk
                 << " rank=" << diag.rank
                 << " sigma0=" << diag.sigma0 << "\n";

        // --- one-epoch dump, for when a number above looks wrong -------------
        if (optDumpSod >= 0.0 && std::fabs(sod - optDumpSod) < 0.5) {
            cout << "\n=== dump sod " << sod << " (" << mode.label << ") ===\n";
            cout << "datum " << datumSat.toString()
                 << "  unknowns " << diag.nUnk << "  equations " << diag.nObs
                 << "  rank " << diag.rank << "  cond " << diag.cond << "\n";
            cout << left;
            for (const auto &oe : equSysDD.obsEquData) {
                cout << setw(5) << oe.first.sat.toString() << setw(4) << oe.first.obsType
                     << " prefit " << setw(14) << oe.second.prefit
                     << " w " << setw(12) << oe.second.weight << "  ";
                for (const auto &vc : oe.second.varCoeffData)
                    cout << "[" << (int) vc.first.getParaType() << ":" << vc.second << "]";
                cout << "\n";
            }
            cout << "state: " << solverRTK.getState().transpose() << "\n";
            cout << "sigma0 " << diag.sigma0 << "  postfit rms " << diag.postfitRms << "\n\n";
        }

        if (haveStop && roverData.epoch > stopEpoch)
            break;
    }

    roverObsStream.close();
    baseObsStream.close();
    solStream.close();
    diagStream.close();

    //---------------------------------------------------------------
    // Manifest
    //---------------------------------------------------------------
    string manifestFile = outDir + "/" + obsBase + "_" + mode.key + "_manifest.json";
    std::ofstream mf(manifestFile, ios::trunc);
    if (mf) {
        mf << "{\n";
        mf << "  \"roverObs\": \"" << roverFile << "\",\n";
        mf << "  \"baseObs\": \"" << baseFile << "\",\n";
        mf << "  \"nav\": \"" << navFile << "\",\n";
        mf << "  \"sys\": \"" << mode.key << "\",\n";
        mf << "  \"sysLabel\": \"" << mode.label << "\",\n";
        mf << "  \"codePair\": [\"" << mode.codePair.first
           << "\", \"" << mode.codePair.second << "\"],\n";
        mf << "  \"rtkFloatOut\": \"" << solFile << "\",\n";
        mf << "  \"rtkDiagOut\": \"" << diagFile << "\",\n";
        mf << "  \"epochs\": " << epochCount << ",\n";
        mf << "  \"epochsSkipped\": "
           << (skipNoRoverSpp + skipSync + skipNoBaseSpp + skipNoSd + skipNoDd) << ",\n";
        mf << "  \"skippedRoverSpp\": " << skipNoRoverSpp << ",\n";
        mf << "  \"skippedSync\": " << skipSync << ",\n";
        mf << "  \"skippedBaseSpp\": " << skipNoBaseSpp << ",\n";
        mf << "  \"skippedTooFewCommonSats\": " << skipNoSd << ",\n";
        mf << "  \"skippedEmptyDoubleDifference\": " << skipNoDd << ",\n";
        mf << "  \"epochsDatumFellBackToIntersection\": " << skipDatumFallback << ",\n";
        mf << "  \"stopUTC\": \"" << cfg.stopUTC << "\",\n";
        mf << "  \"cutOffElevation\": " << cfg.cutOffElevation << "\n";
        mf << "}\n";
        mf.close();
    }

    cout << "Epochs solved       : " << epochCount << "\n";
    cout << "Epochs skipped      : "
         << (skipNoRoverSpp + skipSync + skipNoBaseSpp + skipNoSd + skipNoDd) << "\n";
    if (skipSync)            cout << "  no matching base epoch : " << skipSync << "\n";
    if (skipNoRoverSpp)      cout << "  rover SPP failed       : " << skipNoRoverSpp << "\n";
    if (skipNoBaseSpp)       cout << "  base SPP failed        : " << skipNoBaseSpp << "\n";
    if (skipNoSd)            cout << "  too few common sats    : " << skipNoSd << "\n";
    if (skipNoDd)            cout << "  empty double difference: " << skipNoDd << "\n";
    cout << "Solution   -> " << solFile << "\n";
    cout << "Diagnostics-> " << diagFile << "\n";

    return 0;
}
