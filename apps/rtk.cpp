/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * RTK: carrier-phase relative positioning, chapter 8 of the lecture notes.
 *
 * Both exercises and the chapter's main program live here rather than in three
 * programs, because they differ in one or two steps of a pipeline that is
 * otherwise identical (read two receivers, linearize, difference, solve, fix):
 *
 *   the default      single-epoch least-squares FLOAT solution  (exercise 1)
 *   --fix            integer ambiguity resolution, MLAMBDA      (exercise 2)
 *   --estimator kalman  ambiguities carried across epochs       (8.3.4 / 8.5)
 *   --sys bds23 --isb   the two BeiDou generations in one solution
 *
 * The name is plain `rtk` for that reason. It used to be `rtk_float`, which was
 * accurate when the float solution was all it did and misleading afterwards.
 * The output files keep their `_rtk_float.out` suffix: that file holds the float
 * solution whatever estimator produced it, and its name is pinned by the
 * regression test's byte-exact comparison. Which estimator ran is in the
 * manifest.
 *
 * Each epoch is linearized at its own approximate position with SPPUCCodePhase,
 * the two equation systems are differenced between stations and then between
 * satellites, and the resulting double-difference system is solved. Without
 * `--estimator kalman` each epoch is independent: nothing is fixed to an integer
 * and nothing is carried across epochs.
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
 * ## The fixed solution (`--fix`)
 *
 * With `--fix`, each epoch's ambiguities are resolved to integers by MLAMBDA and
 * the coordinates are corrected with them - the textbook's 8.3.5, equations
 * (8.50)-(8.63). This is the step that makes the carrier phase pay off: an
 * integer constraint removes one free parameter per phase equation, so the phase
 * finally constrains the geometry instead of absorbing it.
 *
 * Two things worth knowing before reading the numbers:
 *
 *   - A single epoch's float ambiguities are only as good as the pseudorange
 *     coordinates they are derived from, so their standard deviations run to a
 *     few tenths of a cycle and many epochs will not pass the ratio test. That
 *     rate is itself a result; tying the ambiguities across epochs (the
 *     textbook's Kalman filter, 8.3.4) is what brings it up.
 *   - When fixing goes wrong the error is the integer slip, in metres - far
 *     worse than the float solution's centimetres. On the zero baseline, whose
 *     true answer is exactly zero, that is directly visible, which is why this
 *     dataset is a good place to measure it.
 *
 * ## Outputs
 *
 *   <rover>_<sys>_rtk_float.out   one line per epoch, the textbook's format
 *   <rover>_<sys>_rtk_fixed.out   the same epochs, fixed, with the ratio - only with --fix
 *   <rover>_<sys>_rtk_diag.csv    per-epoch conditioning, post-fit and fixing diagnostics
 *   <rover>_manifest.json         what produced the above
 *
 * Usage:
 *   rtk [config.ini] [options]
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
 *   --fix                 Resolve the ambiguities and write the fixed solution
 *   --ratio <x>           Ratio-test threshold (default: 3.0)
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
#include "CSDetector.h"
#include "SolverLSQ.h"
#include "SolverKalman.h"
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
         "RTK relative positioning: least-squares (default) or Kalman.\n"
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
         "  --isb              Estimate the receiver inter-system bias between\n"
         "                     BDS-2 and BDS-3 (only meaningful for --sys bds23)\n"
         "  --fix              Resolve ambiguities to integers (MLAMBDA) and\n"
         "                     write <rover>_<sys>_rtk_fixed.out as well\n"
         "  --ratio <x>        Ratio-test threshold for accepting a fix, >= 1\n"
         "                     (default 3.0; implies --fix)\n"
         "  --estimator <e>    lsq (default) or kalman. kalman carries the\n"
         "                     ambiguities across epochs and needs the cycle-slip\n"
         "                     flags; it uses the textbook's 8.3.4 parameterisation\n"
         "  --dump-cs          Run the cycle-slip detectors on both receivers and\n"
         "                     report the flags the Kalman solver would consume\n"
         "                     (writes <rover>_<sys>_cs.csv)\n"
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
///
/// Takes the state vector rather than a solver, because there are two solvers
/// now and both expose getState().
DdDiag diagnoseDd(const EquSys &equSys, const VectorXd &state) {
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

    const VectorXd &x = state;
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

/// JSON-escape a string on its way into the manifest.
///
/// Windows paths are full of backslashes, and a backslash inside a JSON string
/// is only legal when it introduces an escape - so writing a path verbatim
/// produced a manifest that `json.load` rejects outright. The Python reader
/// treats that as "no manifest" and falls back to defaults, which is what kept
/// this invisible: on this machine the defaults happened to be the right
/// answers. Anywhere they were not, a run's own recorded settings would have
/// been silently dropped.
std::string jsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

/// The solved value of one parameter, or 0 when the epoch does not estimate it.
///
/// The state vector has no fixed layout - it is whatever the equation system
/// asked for, minus what the differences removed - so a parameter is looked up
/// by type rather than by position. Used for the inter-system bias, which is
/// present only in a mixed solution and only when its column came out non-zero.
double stateValue(const EquSys &equSys, const VectorXd &state,
                  Parameter::ParameterName type) {
    const VectorXd &x = state;
    int i = 0;
    for (const Variable &v : equSys.varSet) {
        if (v.getParaType() == type)
            return (i < x.size()) ? x(i) : 0.0;
        ++i;
    }
    return 0.0;
}

}  // namespace

int main(int argc, char *argv[]) {

    //---------------------------------------------------------------
    // Command line
    //---------------------------------------------------------------
    string configFile = "config/rtk.ini";
    bool haveConfigArg = false;

    string optObs, optBaseObs, optNav, optOutDir, optStop, optSys;
    string optRatio;
    bool optIsb = false;
    bool optDumpCs = false;
    bool optVerbose = false;
    // "lsq" (default) or "kalman"; empty means "take it from the config".
    string optEstimator;
    bool optFix = false;
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
        else if (a == "--isb")        optIsb = true;
        else if (a == "--dump-cs")    optDumpCs = true;
        else if (a == "--estimator")  optEstimator = needValue("--estimator");
        else if (a == "--fix")        optFix = true;
        else if (a == "--ratio")      optRatio = needValue("--ratio");
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
    if (optIsb)              cfg.estimateISB = true;
    if (!optEstimator.empty()) cfg.estimator = optEstimator;
    if (cfg.estimator != "lsq" && cfg.estimator != "kalman") {
        cerr << "Error: estimator must be 'lsq' or 'kalman', got '"
             << cfg.estimator << "'\n";
        return 2;
    }
    const bool useKalman = (cfg.estimator == "kalman");
    // The filter needs the flags whether or not they were asked for as output.
    const bool wantCsFlags = useKalman || optDumpCs;

    if (optFix)              cfg.fixAmbiguity = true;
    if (!optRatio.empty()) {
        try {
            cfg.ratioThreshold = std::stod(optRatio);
        } catch (const std::exception &) {
            cerr << "Error: --ratio needs a number, got '" << optRatio << "'\n";
            return 2;
        }
        // A fix is accepted when the ratio is strictly greater than this, and
        // the ratio is a quotient of residual sums of squares, so it is >= 1
        // whenever a search succeeds. A threshold below 1 would accept every
        // fix unconditionally; reject it rather than let it look like a result.
        if (!(cfg.ratioThreshold >= 1.0)) {
            cerr << "Error: --ratio must be >= 1, got " << cfg.ratioThreshold << "\n";
            return 2;
        }
        cfg.fixAmbiguity = true;
    }

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
        cout << "fix       : " << (cfg.fixAmbiguity ? "yes" : "no")
             << "  ratio threshold " << cfg.ratioThreshold << "\n";
        cout << "estimator : " << cfg.estimator << "\n";
        cout << "isb       : "
             << (cfg.estimateISB ? "yes (BDS-3 relative to BDS-2)" : "no") << "\n";
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

    std::map<string, std::vector<std::pair<string, string>>> dualCodeTypes;
    dualCodeTypes[mode.system] = mode.codePairs;

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
    sppRover.setEstimateISB(cfg.estimateISB);

    SPPUCCodePhase sppBase;
    sppBase.setRinexNavStore(&navStore);
    sppBase.setStationAsBase();
    sppBase.setDualCodeTypes(dualCodeTypes);
    sppBase.setCutOffElev(cfg.cutOffElevation);
    // Set on the base too, though it cannot matter: the base is linearized at
    // its known position and never solved, and differenceStation() carries only
    // the rover's coefficients across. It is set so the two stations are
    // configured identically, which is easier to reason about than an asymmetry
    // that happens to be harmless.
    sppBase.setEstimateISB(cfg.estimateISB);

    // Tell the cycle-slip detectors which bands this mode uses.
    //
    // They default to a table written for chapter 7 - GPS L1/L2, BeiDou
    // B1I/B2I - and that table cannot see B2a, so on `bds3` they would form no
    // combination for any satellite and every flag would stay zero. That is not
    // a cosmetic loss: the Kalman filter's premise is that an ambiguity is
    // constant across epochs, which holds only until a slip, so a mode with no
    // flags carries a stale ambiguity forever. `bds23` needs the list form for
    // the same reason the code pairs do: its two generations use different
    // second frequencies.
    {
        std::map<string, std::vector<std::pair<string, string>>> bands;
        for (const auto &cp : mode.codePairs) {
            // "C2" -> "L2", "C7" -> "L7": the phase type is the code type with
            // the leading C swapped for an L, the same rule SPPUCCodePhase uses.
            bands[mode.system].push_back({"L" + cp.first.substr(1),
                                          "L" + cp.second.substr(1)});
        }
        setCycleSlipBands(bands);
    }

    // Two detectors, one per receiver. They could be one - the detectors' state
    // tables are keyed by (station, satellite) precisely so the two receivers
    // cannot share a recursion window - but two instances say what is meant.
    // Only exercised by --dump-cs.
    CSDetector csDetRover;
    CSDetector csDetBase;

    SolverLSQ solverRTK;

    // The Kalman path's state. `kalmanFixedAmb` is the previous epoch's fixed
    // ambiguity map, which ambiguityDatum() turns into the next epoch's
    // reference-satellite constraint; `kalmanFirstEpoch` selects the first-epoch
    // form of that constraint - pin the reference to zero, since there is no
    // previous value to pin it to.
    SolverKalman solverKal;
    VariableDataMap kalmanFixedAmb;
    bool kalmanFirstEpoch = true;

    //---------------------------------------------------------------
    // Output files
    //---------------------------------------------------------------
    string obsBase = fileNameOf(roverFile);
    string solFile  = outDir + "/" + obsBase + "_" + mode.key + "_rtk_float.out";
    string fixedFile = outDir + "/" + obsBase + "_" + mode.key + "_rtk_fixed.out";
    string diagFile = outDir + "/" + obsBase + "_" + mode.key + "_rtk_diag.csv";

    std::fstream solStream(solFile, ios::out);
    if (!solStream) {
        cerr << "Error: cannot open solution file: " << solFile << "\n";
        return 1;
    }

    // A separate file rather than extra columns on the float one. The float
    // output is pinned to the original program's output byte for byte by
    // tests/test_rtk_float_regression.py, so adding to it would break the only
    // external anchor this chapter has.
    std::ofstream fixedStream;
    if (cfg.fixAmbiguity) {
        fixedStream.open(fixedFile, ios::out | ios::trunc);
        if (!fixedStream) {
            cerr << "Error: cannot open fixed solution file: " << fixedFile << "\n";
            return 1;
        }
    }

    std::ofstream diagStream(diagFile, ios::trunc);
    if (!diagStream) {
        cerr << "Error: cannot open diagnostics file: " << diagFile << "\n";
        return 1;
    }
    // The fixing columns are always present so that the header does not depend
    // on the flags, and so that the Python reader's column-by-name lookup never
    // has to special-case a run. With fixing off they are 0, which is the same
    // thing ratio 0 means when fixing is on: no integer solution was available.
    diagStream << "sod,nRoverEq,nSD,nDD,nUnk,rank,cond,datumSat,datumFallback,"
                  "nSDsats,absDxyz,sigma0,postfitRms,"
                  "nAmb,ratio,fixed,absDxyzFixed,isb\n";
    diagStream << fixed << setprecision(6);

    // --- the cycle-slip wiring, when asked for ------------------------------
    // Its own file rather than more columns on the diagnostics CSV, because it
    // only exists when --dump-cs is given and the diagnostics file is written
    // on every run.
    string csFile = outDir + "/" + obsBase + "_" + mode.key + "_cs.csv";
    std::ofstream csStream;
    if (optDumpCs) {
        csStream.open(csFile, ios::trunc);
        if (!csStream) {
            cerr << "Error: cannot open cycle-slip file: " << csFile << "\n";
            return 1;
        }
        // One row per NON-ZERO flag, not per epoch: the interesting content is
        // the handful of epochs where something fired, and the ground truth a
        // detector can be scored against is a list of (satellite, epoch) pairs.
        // The per-epoch coverage counts go to the console and the manifest.
        csStream << "sod,sat,system,band,flag\n";
    }
    long csEpochsWithSlip = 0;
    long csUncoveredTotal = 0;
    long csFlagTotal = 0;
    std::map<string, long> csUncoveredByKey;

    //---------------------------------------------------------------
    // Epoch loop
    //---------------------------------------------------------------
    long epochCount = 0;
    long skipNoRoverSpp = 0, skipSync = 0, skipNoBaseSpp = 0;
    long skipNoSd = 0, skipNoDd = 0, skipDatumFallback = 0;
    long fixedEpochs = 0;      // epochs whose ratio cleared the threshold

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

        // --- cycle-slip flags (--dump-cs) ------------------------------------
        // Detected AFTER both single-point solves. That order is the lecture
        // notes' own and it is load-bearing: the detectors drop the satellites
        // they cannot form a combination for, so running them first would take
        // those satellites out of the equations as well.
        VariableDataMap csFlagRover, csFlagBase, csFlagSd;
        if (wantCsFlags) {
            csFlagRover = csDetRover.detect(roverData);
            csFlagBase  = csDetBase.detect(baseData);
        }

        // --- between stations ------------------------------------------------
        EquSys equSysSD;
        if (wantCsFlags) {
            // The flag-carrying overload: the same equation system (it calls
            // the three-argument one internally) plus the two receivers' slip
            // flags merged into a single map keyed by ambiguity variable.
            // THIS map is the csData the Kalman solver takes.
            differenceStation(equSysRover, csFlagRover, equSysBase, csFlagBase,
                              equSysSD, csFlagSd);
        } else {
            differenceStation(equSysRover, equSysBase, equSysSD);
        }

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
        VariableDataMap csFlagDd;
        if (useKalman) {
            // The five-argument overload, and the reason the Kalman path cannot
            // use the three-argument one: it KEEPS the reference satellite's
            // ambiguity (with coefficient -lambda) instead of differencing it
            // away, so the ambiguity variables are station-difference
            // ambiguities whose meaning does not depend on which satellite is
            // the reference. A filter has to carry them across epochs, and the
            // reference changes two or three times per run.
            //
            // The price is one rank deficiency, which the textbook removes with
            // a constraint equation (8.3.4.3) - that is ambiguityDatum(), and it
            // must be added before the solve.
            differenceSat(datumSat, equSysSD, csFlagSd, equSysDD, csFlagDd);
        } else {
            differenceSat(datumSat, equSysSD, equSysDD);
        }

        if (equSysDD.obsEquData.empty() || equSysDD.varSet.size() < 3) {
            ++skipNoDd;
            if (optVerbose)
                cout << "[skip] sod " << sod << " empty double-difference system ("
                     << equSysDD.obsEquData.size() << " obs, "
                     << equSysDD.varSet.size() << " unknowns)\n";
            continue;
        }

        // --- cycle-slip coverage ---------------------------------------------
        // What the filter needs is not the number of flags but whether EVERY
        // ambiguity it carries has one. An ambiguity with no entry at all is one
        // the filter would propagate as a constant whatever the observations
        // did - a silent failure, and the reason this is counted rather than
        // just summing the flags. The missing keys are collected by name: they
        // are band pairs, and they say exactly which frequencies the detectors
        // cannot see.
        if (wantCsFlags) {
            int nCsNonZero = 0, nCsUncovered = 0;
            for (const auto &kv : csFlagSd)
                if (kv.second != 0.0) ++nCsNonZero;
            for (const Variable &v : equSysDD.varSet) {
                if (v.getParaType() != Parameter::ambiguity) continue;
                if (csFlagSd.find(v) == csFlagSd.end()) {
                    ++nCsUncovered;
                    ++csUncoveredByKey[v.getObsID().toString()];
                }
            }
            if (nCsNonZero > 0) {
                ++csEpochsWithSlip;
                for (const auto &kv : csFlagSd) {
                    if (kv.second == 0.0) continue;
                    if (!optDumpCs) break;   // counted either way, written only on request
                    csStream << sod << ","
                             << kv.first.getSat().toString() << ","
                             << kv.first.getObsID().satSys << ","
                             << kv.first.getObsID().obsType << ","
                             << (int) kv.second << "\n";
                }
            }
            csUncoveredTotal += nCsUncovered;
            csFlagTotal += (long) csFlagSd.size();
        }

        // --- solve -----------------------------------------------------------
        // One state vector and one covariance from here on, whichever solver
        // ran: the diagnostics, the inter-system bias lookup and fixSolution()
        // all work off these two, and neither needs to know which produced them.
        VectorXd stateVec;
        MatrixXd covMatrix;
        Vector3d dxyzRTK;
        try {
            if (useKalman) {
                // The reference ambiguity constraint, from this epoch's own
                // reference satellite. On the first epoch it pins it to zero;
                // afterwards it pins the new reference to the value the previous
                // epoch fixed it to, which is what keeps the ambiguity set
                // continuous when the reference satellite changes.
                ambiguityDatum(kalmanFirstEpoch, datumSat, kalmanFixedAmb, equSysDD);
                solverKal.solve(equSysDD, csFlagDd);
                stateVec = solverKal.getState();
                covMatrix = solverKal.getCovMatrix();
                dxyzRTK = solverKal.getdxyz();
            } else {
                solverRTK.solve(equSysDD);
                stateVec = solverRTK.getState();
                covMatrix = solverRTK.getCovMatrix();
                dxyzRTK = solverRTK.getdxyz();
            }
        }
        catch (std::exception &e) {
            ++skipNoDd;
            if (optVerbose)
                cout << "[skip] sod " << sod << " DD solve: " << e.what() << "\n";
            continue;
        }
        Vector3d xyzRover = sppRover.getXYZ();
        Vector3d xyzRTKFloat = xyzRover + dxyzRTK;

        // --- ambiguity resolution --------------------------------------------
        // Textbook 8.3.5: MLAMBDA on the float solution's ambiguity block, then
        // the coordinate correction of (8.63). fixSolution() signals failure
        // through the ratio, not a return code - a ratio of 0 means no integer
        // candidate exists, and dxyzFixed then comes back equal to the float
        // increment, so the "fixed" line is simply the float one. Applying the
        // threshold is the caller's job, which is what `fixed` records.
        double ratio = 0.0;
        Vector3d dxyzFixed = dxyzRTK;
        bool fixedAccepted = false;
        if (cfg.fixAmbiguity) {
            // fixSolution() writes only the ambiguity map it was given, so the
            // previous epoch's map is copied first and then updated in place.
            // The Kalman path needs it to persist: the reference-satellite
            // constraint of the NEXT epoch is built from this epoch's fix.
            VariableDataMap fixedAmbData = kalmanFixedAmb;
            fixSolution(stateVec, covMatrix, equSysDD.varSet,
                        ratio, dxyzFixed, fixedAmbData);
            fixedAccepted = (ratio > cfg.ratioThreshold);
            if (fixedAccepted) ++fixedEpochs;
            if (useKalman) kalmanFixedAmb = fixedAmbData;
        }
        Vector3d xyzRTKFixed = xyzRover + dxyzFixed;

        // Counted from the equation system rather than from fixedAmbData, so
        // that it is the same number whether fixing ran or not.
        int nAmb = 0;
        for (const Variable &v : equSysDD.varSet)
            if (v.getParaType() == Parameter::ambiguity) ++nAmb;

        DdDiag diag = diagnoseDd(equSysDD, stateVec);

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
                   << diag.postfitRms << ","
                   << nAmb << ","
                   << ratio << ","
                   << (fixedAccepted ? 1 : 0) << ","
                   << dxyzFixed.norm() << ","
                   << stateValue(equSysDD, stateVec, Parameter::ifb) << "\n";

        printSolution(solStream, epoch, xyzRover, xyzRTKFloat);
        if (cfg.fixAmbiguity)
            printSolution(fixedStream, epoch, xyzRover, xyzRTKFloat, ratio, xyzRTKFixed);
        ++epochCount;
        // From the second solved epoch on, ambiguityDatum() pins the reference
        // ambiguity to the previous epoch's fixed value instead of to zero.
        kalmanFirstEpoch = false;

        if (optVerbose)
            cout << "[rtk] sod " << sod
                 << "  dxyz " << dxyzRTK.transpose()
                 << "  n=" << diag.nObs << "/" << diag.nUnk
                 << " rank=" << diag.rank
                 << " sigma0=" << diag.sigma0
                 << (cfg.fixAmbiguity
                         ? ("  ratio=" + to_string(ratio) + (fixedAccepted ? " FIXED" : " float"))
                         : "")
                 << "\n";

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
            // With --dump-cs as well, show why every ambiguity did or did not
            // find a flag: the two key sets side by side. This is the check
            // that matters for the filter - a key that is absent means an
            // ambiguity nothing would ever reset.
            if (optDumpCs) {
                cout << "csFlagSd keys (" << csFlagSd.size() << "):\n";
                for (const auto &kv : csFlagSd)
                    cout << "    " << kv.first << " = " << kv.second << "\n";
                cout << "equSysDD ambiguities:\n";
                for (const Variable &v : equSysDD.varSet)
                    if (v.getParaType() == Parameter::ambiguity)
                        cout << "    " << v
                             << (csFlagSd.count(v) ? "   <- has a flag" : "   <- NO FLAG")
                             << "\n";
            }
            cout << "state: " << stateVec.transpose() << "\n";
            cout << "sigma0 " << diag.sigma0 << "  postfit rms " << diag.postfitRms << "\n\n";
        }

        if (haveStop && roverData.epoch > stopEpoch)
            break;
    }

    roverObsStream.close();
    baseObsStream.close();
    solStream.close();
    if (cfg.fixAmbiguity) fixedStream.close();
    diagStream.close();
    if (optDumpCs) csStream.close();

    //---------------------------------------------------------------
    // Manifest
    //---------------------------------------------------------------
    string manifestFile = outDir + "/" + obsBase + "_" + mode.key + "_manifest.json";
    std::ofstream mf(manifestFile, ios::trunc);
    if (mf) {
        mf << "{\n";
        mf << "  \"roverObs\": \"" << jsonEscape(roverFile) << "\",\n";
        mf << "  \"baseObs\": \"" << jsonEscape(baseFile) << "\",\n";
        mf << "  \"nav\": \"" << jsonEscape(navFile) << "\",\n";
        mf << "  \"sys\": \"" << jsonEscape(mode.key) << "\",\n";
        mf << "  \"sysLabel\": \"" << jsonEscape(mode.label) << "\",\n";
        mf << "  \"estimator\": \"" << jsonEscape(cfg.estimator) << "\",\n";
        mf << "  \"isbEstimated\": " << (cfg.estimateISB ? "true" : "false") << ",\n";
        // A list, because a merged BDS-2 + BDS-3 mode has one pair per
        // generation. The single-generation modes still write exactly one.
        mf << "  \"codePairs\": [";
        for (size_t k = 0; k < mode.codePairs.size(); ++k) {
            mf << (k ? ", " : "") << "[\"" << jsonEscape(mode.codePairs[k].first)
               << "\", \"" << jsonEscape(mode.codePairs[k].second) << "\"]";
        }
        mf << "],\n";
        mf << "  \"rtkFloatOut\": \"" << jsonEscape(solFile) << "\",\n";
        if (wantCsFlags) {
            if (optDumpCs) {
                mf << "  \"csOut\": \"" << jsonEscape(csFile) << "\",\n";
            }
            mf << "  \"csEpochsWithSlip\": " << csEpochsWithSlip << ",\n";
            mf << "  \"csAmbiguityFlags\": " << csFlagTotal << ",\n";
            mf << "  \"csAmbiguitiesWithoutFlag\": " << csUncoveredTotal << ",\n";
            mf << "  \"csBandsNotCovered\": [";
            {
                bool first = true;
                for (const auto &kv : csUncoveredByKey) {
                    mf << (first ? "" : ", ") << "\"" << jsonEscape(kv.first) << "\"";
                    first = false;
                }
            }
            mf << "],\n";
        }
        if (cfg.fixAmbiguity) {
            mf << "  \"rtkFixedOut\": \"" << jsonEscape(fixedFile) << "\",\n";
            mf << "  \"fixAmbiguity\": true,\n";
            mf << "  \"ratioThreshold\": " << cfg.ratioThreshold << ",\n";
            mf << "  \"epochsFixed\": " << fixedEpochs << ",\n";
            mf << "  \"fixedFraction\": "
               << (epochCount ? (double) fixedEpochs / (double) epochCount : 0.0) << ",\n";
        } else {
            mf << "  \"fixAmbiguity\": false,\n";
        }
        mf << "  \"rtkDiagOut\": \"" << jsonEscape(diagFile) << "\",\n";
        mf << "  \"epochs\": " << epochCount << ",\n";
        mf << "  \"epochsSkipped\": "
           << (skipNoRoverSpp + skipSync + skipNoBaseSpp + skipNoSd + skipNoDd) << ",\n";
        mf << "  \"skippedRoverSpp\": " << skipNoRoverSpp << ",\n";
        mf << "  \"skippedSync\": " << skipSync << ",\n";
        mf << "  \"skippedBaseSpp\": " << skipNoBaseSpp << ",\n";
        mf << "  \"skippedTooFewCommonSats\": " << skipNoSd << ",\n";
        mf << "  \"skippedEmptyDoubleDifference\": " << skipNoDd << ",\n";
        mf << "  \"epochsDatumFellBackToIntersection\": " << skipDatumFallback << ",\n";
        mf << "  \"stopUTC\": \"" << jsonEscape(cfg.stopUTC) << "\",\n";
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
    if (wantCsFlags) {
        cout << "Cycle-slip flags    : " << csEpochsWithSlip << " epoch(s) with a "
             << "non-zero flag\n";
        if (csUncoveredTotal > 0) {
            cout << "  ambiguity variables with NO flag: " << csUncoveredTotal
                 << "  (the detectors cannot see:";
            for (const auto &kv : csUncoveredByKey)
                cout << " " << kv.first;
            cout << ")\n";
            cout << "  an ambiguity with no flag is one the filter would carry"
                 << " across a slip without noticing\n";
        } else {
            cout << "  every double-difference ambiguity carries a flag\n";
        }
        if (optDumpCs) cout << "Cycle slip -> " << csFile << "\n";
    }
    if (cfg.fixAmbiguity) {
        cout << "Epochs fixed        : " << fixedEpochs;
        if (epochCount)
            cout << "  (" << fixed << setprecision(1)
                 << (100.0 * (double) fixedEpochs / (double) epochCount) << "%)";
        cout << "  at ratio > " << cfg.ratioThreshold << "\n";
        cout << "Fixed      -> " << fixedFile << "\n";
    }
    cout << "Solution   -> " << solFile << "\n";
    cout << "Diagnostics-> " << diagFile << "\n";

    return 0;
}
