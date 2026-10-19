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
 *   --sys <mode>          gps | bds2 | bds3 | bds23 | gpsbds (default: gps)
 *   --bds-max-sats <n>    Mixed solution: cap on BeiDou satellites (0 = all)
 *   --bds-orbit-ratio <g,i,m>  GEO:IGSO:MEO relative slots (default 1,1,1)
 *   --sigma-factor-gps|bd2|bd3 <x>   Code sigma multiplier per system
 *   --sigma-factor-geo|igso|meo <x>  Code sigma multiplier per BDS orbit
 *   --dump-orbit-types    Print the BeiDou orbital classification and exit
 *   --trop                Apply the troposphere-delay correction (off by default)
 *   --bds-tgd             Apply the BeiDou TGD correction (bds* only, off by default)
 *   --fix                 Resolve the ambiguities and write the fixed solution
 *   --ratio <x>           Ratio-test threshold (default: 3.0)
 *   --dump-epoch <sod>    Print the double-difference system for one epoch
 *   --verbose             Per-epoch progress on stdout
 *   -h, --help            This message
 */

#include <string>
#include <sstream>
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
#include <array>
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
#include "SP3Store.hpp"
#include "SinexBiasStore.hpp"
#include "CarrierSmoother.h"
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
         "  --bds-max-sats <n> Cap the BeiDou satellites in a gpsbds solution\n"
         "                     (0 = keep every satellite; default 0)\n"
         "  --bds-orbit-ratio <g,i,m>\n"
         "                     Relative GEO:IGSO:MEO slots when the cap binds\n"
         "                     (default 1,1,1; 0 excludes a family)\n"
         "  --sigma-factor-gps <x>   Code sigma multiplier for GPS (default 1)\n"
         "  --sigma-factor-bd2 <x>   Code sigma multiplier for BDS-2\n"
         "  --sigma-factor-bd3 <x>   Code sigma multiplier for BDS-3\n"
         "  --sigma-factor-geo <x>   Extra multiplier for BeiDou GEO\n"
         "  --sigma-factor-igso <x>  Extra multiplier for BeiDou IGSO\n"
         "  --sigma-factor-meo <x>   Extra multiplier for BeiDou MEO\n"
         "                     >1 downweights (larger sigma), <1 upweights.\n"
         "                     Applied to the double-difference code weights;\n"
         "                     the spp: column is left at the built-in sigma.\n"
         "  --dump-orbit-types Print each BeiDou satellite's orbit family and exit\n"
         "  --isb              Estimate the receiver inter-system bias between\n"
         "                     BDS-2 and BDS-3 (only meaningful for --sys bds23)\n"
         "  --trop             Apply the troposphere-delay correction in the\n"
         "                     single-point solves. Off by default; it moves the\n"
         "                     spp: column, and with it the byte-anchored rtk: one\n"
         "  --bds-tgd          Apply the BeiDou broadcast TGD correction in the\n"
         "                     single-point solves. bds2/bds3/bds23 only - GPS\n"
         "                     already has its TGD folded into the satellite clock\n"
         "  --sp3 <file>       Precise orbit (SP3). With --clk, both single-point\n"
         "  --clk <file>       solves run on precise orbits and clocks instead of\n"
         "                     the broadcast ephemeris. Both are required together;\n"
         "                     the products are not relativity-corrected, so the\n"
         "                     solver adds that term as it does for broadcast\n"
         "  --osb <file>       SINEX-BIAS/OSB file of per-signal code biases.\n"
         "                     Requires --sp3 and --clk: a bias product is defined\n"
         "                     against one analysis center's clock datum. Use the\n"
         "                     orbit, clock and bias files of the same center and\n"
         "                     day. Corrects the code observations of both\n"
         "                     single-point solves, so it moves spp:. A satellite\n"
         "                     bias cancels in the double difference, so rtk: and\n"
         "                     the fix decisions stay put apart from the changed\n"
         "                     linearization point\n"
         "  --smooth <N>       Hatch-smooth the code with its carrier before the\n"
         "                     single-point solves, window N epochs (0 = off, the\n"
         "                     default). Moves the spp: column, and the rtk: one\n"
         "                     with it, so the byte-exact anchor needs it off\n"
         "  --dd-cov           Weight the double differences with their full\n"
         "                     covariance Q_DD = C*Q_SD*C^T (chapter 8 exercise 3)\n"
         "                     instead of treating them as independent. Off by\n"
         "                     default - the byte-exact anchor is the independent\n"
         "                     path.\n"
         "  --base-xyz X,Y,Z   Override the base station's assumed position (its\n"
         "                     RINEX header coordinate) with a known one. The\n"
         "                     double difference resolves the rover against it,\n"
         "                     so it is the datum of the whole chapter\n"
         "  --est-trop         Estimate the residual zenith tropospheric delay as\n"
         "                     a station unknown instead of trusting the model.\n"
         "                     Combine with --trop to correct the model, or use\n"
         "                     alone to estimate the whole delay\n"
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

/* 把双差设计阵重新拼出来，算秩、条件数、验后残差 σ₀、postfit RMS
库里没人检查拟合好坏，SolverLSQ::solve算完就停——一个烂历元从输出文件看跟好历元一模一样。
这几个数是唯一能看出来的地方。*/

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

/*原来的 SPPIFCode::getDatumSat()只按流动站能看到谁选最高仰角，完全不管基准站。
万一基准站没跟踪那颗星，站间差分后它的方程全没了，星间差分就找不到基准方程可减，整个双差系统直接空掉。
这里改成"在站间差分后还活着的卫星里挑最高仰角"，从根上消掉这个失败模式；
同时用usedFallback 报告这次选择是否跟"只看流动站"的选择不同，免得悄悄换了基准星没人发现。*/

/**
 * One reference satellite per constellation.
 *
 * A double difference has to use a reference from the same constellation: the
 * satellite clock and hardware delay it removes are GPS terms for GPS and
 * BeiDou terms for BeiDou, and neither cancels the other. For the single-system
 * modes this returns exactly the satellite the old pickDatumSat() did, so their
 * byte anchors do not move.
 *
 * `usedFallback` keeps its old meaning - the chosen datum is not the rover's
 * own highest-elevation satellite - and for a mixed solution it is set if ANY
 * constellation had to fall back.
 */
std::map<string, SatID> pickDatumSats(const EquSys &equSysSD,
                                      const SatValueMap &satElevData,
                                      bool &usedFallback) {
    set<SatID> sdSats;
    for (const auto &oe : equSysSD.obsEquData)
        sdSats.insert(oe.first.sat);

    std::map<string, SatID> best;
    std::map<string, double> bestElev;
    for (const SatID &s : sdSats) {
        auto it = satElevData.find(s);
        double elev = (it == satElevData.end()) ? 0.0 : it->second;
        auto bi = best.find(s.system);
        if (bi == best.end()) {
            best[s.system] = s;
            bestElev[s.system] = elev;
        } else if (elev > bestElev[s.system] ||
                   (elev == bestElev[s.system] && s < bi->second)) {
            // The id tie-break makes the choice deterministic when two
            // satellites have exactly the same elevation.
            best[s.system] = s;
            bestElev[s.system] = elev;
        }
    }

    // Report whether this agrees with the rover-only choice, so a change in
    // which satellite anchored an epoch is visible rather than silent. The
    // comparison is per constellation: in a mixed solution the global top
    // satellite is always one of the two, so comparing every datum against it
    // would report a fallback on every single epoch.
    std::map<string, SatID> roverTop;
    std::map<string, double> roverTopElev;
    for (const auto &se : satElevData) {
        auto it = roverTopElev.find(se.first.system);
        if (it == roverTopElev.end() || se.second > it->second ||
            (se.second == it->second && se.first < roverTop[se.first.system])) {
            roverTopElev[se.first.system] = se.second;
            roverTop[se.first.system] = se.first;
        }
    }
    usedFallback = false;
    for (const auto &kv : best) {
        auto it = roverTop.find(kv.first);
        if (it != roverTop.end() && !(kv.second == it->second))
            usedFallback = true;
    }
    return best;
}

/// Which orbital families the BeiDou selection covers, in quota order.
int orbitIndex(BdsOrbitType type) {
    switch (type) {
        case BdsOrbitType::GEO: return 0;
        case BdsOrbitType::IGSO: return 1;
        case BdsOrbitType::MEO: return 2;
        default: return 2;   // no ephemeris: treat like a MEO, the fallback
    }
}

/// Counts for the diagnostics: candidates seen in the epoch, and selected.
struct BdsOrbitCounts {
    int candidate[3] = {0, 0, 0};
    int selected[3] = {0, 0, 0};
};

/**
 * Choose the BeiDou satellites that take part in the mixed solution.
 *
 * `ratio` is the relative number of GEO : IGSO : MEO slots and `maxSats` the
 * total cap. With no cap (or with fewer candidates than the cap) everything is
 * kept, so the default mixed run loses nothing. With a cap, each family gets
 * its proportional share of the slots, the remainder goes by largest fractional
 * part, and within a family the highest-elevation satellites win. If a family
 * cannot fill its share, the spare slots go to the other enabled families.
 *
 * A zero quota excludes that family outright. All-zero is rejected at the
 * command line rather than silently producing a GPS-only solution.
 */
std::vector<SatID> selectBdsByOrbit(const set<SatID> &sdSats,
                                    const SatValueMap &satElevData,
                                    const RinexNavStore &navStore,
                                    const std::array<int, 3> &ratio,
                                    int maxSats,
                                    BdsOrbitCounts &counts) {
    std::vector<SatID> candidates[3];
    for (const SatID &s : sdSats) {
        if (s.system != "C") continue;
        int k = orbitIndex(navStore.bdsOrbitType(s));
        candidates[k].push_back(s);
        ++counts.candidate[k];
    }

    const int total = (int) (candidates[0].size() + candidates[1].size() +
                             candidates[2].size());
    if (maxSats <= 0 || maxSats >= total) {
        // No cap (or a cap larger than the sky): keep every candidate of the
        // families the ratio enables. A zero quota still excludes a family,
        // which is how "MEO + IGSO only, no cap" is expressed.
        std::vector<SatID> all;
        for (int k = 0; k < 3; ++k) {
            if (ratio[k] == 0) continue;
            counts.selected[k] = (int) candidates[k].size();
            all.insert(all.end(), candidates[k].begin(), candidates[k].end());
        }
        return all;
    }

    auto higher = [&](const SatID &a, const SatID &b) {
        auto ea = satElevData.find(a);
        auto eb = satElevData.find(b);
        double va = (ea == satElevData.end()) ? 0.0 : ea->second;
        double vb = (eb == satElevData.end()) ? 0.0 : eb->second;
        if (va != vb) return va > vb;
        return a < b;
    };
    for (auto &c : candidates) std::sort(c.begin(), c.end(), higher);

    int desired[3] = {0, 0, 0};
    const int ratioSum = ratio[0] + ratio[1] + ratio[2];
    double frac[3] = {0.0, 0.0, 0.0};
    int assigned = 0;
    for (int k = 0; k < 3; ++k) {
        double exact = (double) maxSats * ratio[k] / (double) ratioSum;
        desired[k] = (int) std::floor(exact);
        frac[k] = exact - desired[k];
        assigned += desired[k];
    }
    // Largest remainder first; ties go to GEO, then IGSO (k order).
    while (assigned < maxSats) {
        int pick = -1;
        for (int k = 0; k < 3; ++k) {
            if (ratio[k] == 0) continue;
            if (desired[k] >= (int) candidates[k].size()) continue;
            if (pick < 0 || frac[k] > frac[pick]) pick = k;
        }
        if (pick < 0) break;
        ++desired[pick];
        frac[pick] = -1.0;   // only one extra slot per family per pass
        ++assigned;
    }
    // A family that cannot fill its slots hands them to the others.
    for (int k = 0; k < 3; ++k) {
        if (desired[k] <= (int) candidates[k].size()) continue;
        int spill = desired[k] - (int) candidates[k].size();
        desired[k] = (int) candidates[k].size();
        for (int other = 0; other < 3 && spill > 0; ++other) {
            if (other == k || ratio[other] == 0) continue;
            int room = (int) candidates[other].size() - desired[other];
            int take = std::min(room, spill);
            desired[other] += take;
            spill -= take;
        }
    }

    std::vector<SatID> selected;
    for (int k = 0; k < 3; ++k) {
        int take = std::min(desired[k], (int) candidates[k].size());
        counts.selected[k] = take;
        selected.insert(selected.end(), candidates[k].begin(),
                        candidates[k].begin() + take);
    }
    return selected;
}

/// Code sigma multipliers, one per system and one per BeiDou orbital family.
struct CodeSigmaFactors {
    double gps = 1.0;
    double bd2 = 1.0;
    double bd3 = 1.0;
    double geo = 1.0;
    double igso = 1.0;
    double meo = 1.0;
};

/// Effective code sigma multiplier for one satellite.
double codeSigmaFactorFor(const SatID &sat, const CodeSigmaFactors &f,
                          const RinexNavStore &navStore) {
    if (sat.system == "G") return f.gps;
    if (sat.system != "C") return 1.0;
    double factor = (bdsGeneration(sat) == 2) ? f.bd2 : f.bd3;
    switch (navStore.bdsOrbitType(sat)) {
        case BdsOrbitType::GEO:  factor *= f.geo;  break;
        case BdsOrbitType::IGSO: factor *= f.igso; break;
        case BdsOrbitType::MEO:  factor *= f.meo;  break;
        default: break;
    }
    return factor;
}

/**
 * Scale the CODE weights of a linearized system by the system/family factors.
 *
 * The linearizer has already applied its 0.3 m code sigma and the elevation
 * weighting, so this is a pure multiplier: weight /= factor^2. Phase equations
 * are deliberately untouched - the carrier phase is at the millimetre level for
 * every constellation here, and the lever we are pulling is the code noise that
 * dominates the float solution.
 *
 * Called on both the rover and base systems before station differencing, so the
 * factors survive into the double-difference weights (and into --dd-cov).
 */
void applyCodeSigmaFactors(EquSys &equSys, const CodeSigmaFactors &f,
                           const RinexNavStore &navStore) {
    for (auto &oe : equSys.obsEquData) {
        const string &type = oe.first.obsType;
        if (type.empty() || type[0] != 'C') continue;
        double factor = codeSigmaFactorFor(oe.first.sat, f, navStore);
        if (factor != 1.0)
            oe.second.weight /= factor * factor;
    }
}

/// Erase every satellite not in `keep` from a station-difference system.
void keepOnlySats(EquSys &equSys, const set<SatID> &keep) {
    for (auto it = equSys.obsEquData.begin(); it != equSys.obsEquData.end(); ) {
        if (!keep.count(it->first.sat))
            it = equSys.obsEquData.erase(it);
        else
            ++it;
    }
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
    // Command line 命令行解析
    //---------------------------------------------------------------
    string configFile = "config/rtk.ini";
    bool haveConfigArg = false;

    string optObs, optBaseObs, optNav, optOutDir, optStop, optSys;
    string optRatio;
    bool optIsb = false;
    bool optTrop = false;
    bool optBdsTgd = false;
    bool optDumpCs = false;
    bool optVerbose = false;
    // "lsq" (default) or "kalman"; empty means "take it from the config".
    string optEstimator;
    bool optFix = false;
    double optDumpSod = -1.0;
    // Override the BASE station's assumed position (its RINEX header
    // coordinate is what the double difference resolves against). The
    // zero-baseline set's headers sit ~25 m from the antenna - see
    // docs/rtk.md - so a run wanting a true datum passes one here.
    string optBaseXyz;
    // Precise products, both or neither: --sp3 <orbit file> --clk <clock file>.
    // With them the single-point solves run on IGS/CODE orbits and clocks
    // instead of the broadcast ephemeris, which is how the chapter measures
    // what the broadcast ephemeris was costing the spp: column.
    string optSp3;
    string optClk;
    // Optional SINEX-BIAS/OSB file. It is tied to the precise clock datum, so
    // it is only accepted together with --sp3 and --clk.
    string optOsb;
    // Hatch carrier-smoothing window in epochs; 0 leaves the code raw, which
    // is what every byte-anchored run in this chapter used.
    int optSmooth = 0;
    // Estimate the residual zenith tropospheric delay as a station unknown.
    bool optEstTrop = false;
    // Chapter 8 exercise 3: weight the double differences with their full
    // covariance Q_DD = C*Q_SD*C^T instead of treating them as independent.
    bool optDdCov = false;
    // Mixed GPS + BeiDou: how many BeiDou satellites to carry, and in what
    // GEO : IGSO : MEO proportion. -1 / empty mean "not given on the command
    // line", so the config file's value survives.
    int optBdsMaxSats = -1;
    string optBdsOrbitRatio;
    bool optDumpOrbitTypes = false;
    // Code sigma multipliers; 0 means "not given", because a valid factor is
    // strictly positive.
    double optSigmaGPS = 0.0, optSigmaBD2 = 0.0, optSigmaBD3 = 0.0;
    double optSigmaGEO = 0.0, optSigmaIGSO = 0.0, optSigmaMEO = 0.0;

    for (int i = 1; i < argc; ++i) {
        string a = argv[i];

        auto needValue = [&](const char *name) -> string {
            if (i + 1 >= argc) {
                cerr << "Error: " << name << " requires a value\n";
                exit(2);
            }
            return argv[++i];
        };
        auto needPositive = [&](const char *name) -> double {
            string v = needValue(name);
            try {
                double x = std::stod(v);
                if (!(x > 0.0) || !std::isfinite(x)) throw std::invalid_argument(v);
                return x;
            } catch (const std::exception &) {
                cerr << "Error: " << name << " wants a positive number, got '"
                     << v << "'\n";
                exit(2);
            }
        };

        if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else if (a == "--obs")        optObs = needValue("--obs");
        else if (a == "--base-obs")   optBaseObs = needValue("--base-obs");
        else if (a == "--nav")        optNav = needValue("--nav");
        else if (a == "--out-dir")    optOutDir = needValue("--out-dir");
        else if (a == "--stop")       optStop = needValue("--stop");
        else if (a == "--sys")        optSys = needValue("--sys");
        else if (a == "--isb")        optIsb = true;
        else if (a == "--trop")       optTrop = true;
        else if (a == "--bds-tgd")    optBdsTgd = true;
        else if (a == "--dump-cs")    optDumpCs = true;
        else if (a == "--estimator")  optEstimator = needValue("--estimator");
        else if (a == "--fix")        optFix = true;
        else if (a == "--ratio")      optRatio = needValue("--ratio");
        else if (a == "--dump-epoch") optDumpSod = std::stod(needValue("--dump-epoch"));
        else if (a == "--sp3")        optSp3 = needValue("--sp3");
        else if (a == "--clk")        optClk = needValue("--clk");
        else if (a == "--osb")        optOsb = needValue("--osb");
        else if (a == "--smooth")     optSmooth = std::stoi(needValue("--smooth"));
        else if (a == "--base-xyz")   optBaseXyz = needValue("--base-xyz");
        else if (a == "--est-trop")   optEstTrop = true;
        else if (a == "--dd-cov")     optDdCov = true;
        else if (a == "--bds-max-sats") {
            try {
                optBdsMaxSats = std::stoi(needValue("--bds-max-sats"));
            } catch (const std::exception &) {
                cerr << "Error: --bds-max-sats needs an integer\n";
                return 2;
            }
        }
        else if (a == "--bds-orbit-ratio") optBdsOrbitRatio = needValue("--bds-orbit-ratio");
        else if (a == "--dump-orbit-types") optDumpOrbitTypes = true;
        else if (a == "--sigma-factor-gps")  optSigmaGPS  = needPositive("--sigma-factor-gps");
        else if (a == "--sigma-factor-bd2")  optSigmaBD2  = needPositive("--sigma-factor-bd2");
        else if (a == "--sigma-factor-bd3")  optSigmaBD3  = needPositive("--sigma-factor-bd3");
        else if (a == "--sigma-factor-geo")  optSigmaGEO  = needPositive("--sigma-factor-geo");
        else if (a == "--sigma-factor-igso") optSigmaIGSO = needPositive("--sigma-factor-igso");
        else if (a == "--sigma-factor-meo")  optSigmaMEO  = needPositive("--sigma-factor-meo");
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
    // Configuration 配置
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
    if (!optOsb.empty())     cfg.osbFile = optOsb;
    if (!optStop.empty())    cfg.stopUTC = optStop;
    if (!optSys.empty())     cfg.sys = optSys;
    if (optIsb)              cfg.estimateISB = true;
    if (optTrop)             cfg.enableTrop = true;
    if (optBdsTgd)           cfg.enableBDSTGD = true;
    if (optBdsMaxSats >= 0)  cfg.bdsMaxSats = optBdsMaxSats;
    if (!optBdsOrbitRatio.empty()) cfg.bdsOrbitRatio = optBdsOrbitRatio;
    if (optSigmaGPS  > 0) cfg.codeSigmaFactorGPS  = optSigmaGPS;
    if (optSigmaBD2  > 0) cfg.codeSigmaFactorBD2  = optSigmaBD2;
    if (optSigmaBD3  > 0) cfg.codeSigmaFactorBD3  = optSigmaBD3;
    if (optSigmaGEO  > 0) cfg.codeSigmaFactorGEO  = optSigmaGEO;
    if (optSigmaIGSO > 0) cfg.codeSigmaFactorIGSO = optSigmaIGSO;
    if (optSigmaMEO  > 0) cfg.codeSigmaFactorMEO  = optSigmaMEO;
    if (!optEstimator.empty()) cfg.estimator = optEstimator;
    if (!cfg.osbFile.empty() && (optSp3.empty() || optClk.empty())) {
        cerr << "Error: an OSB file (--osb or osbFile) requires both --sp3 and --clk\n"
             << "       An OSB is defined relative to one analysis center's\n"
             << "       precise clock datum; against a broadcast clock it would\n"
             << "       corrupt rather than correct the observations.\n";
        return 2;
    }

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

    // GEO : IGSO : MEO, three non-negative integers with at least one non-zero.
    // Parsed once here so a bad value stops the run before any file is opened.
    std::array<int, 3> bdsOrbitRatio = {1, 1, 1};
    {
        string s = cfg.bdsOrbitRatio;
        std::replace(s.begin(), s.end(), ',', ' ');
        std::stringstream ss(s);
        int a = 0, b = 0, c = 0;
        string extra;
        if (!(ss >> a >> b >> c) || (ss >> extra)) {
            cerr << "Error: bdsOrbitRatio wants three integers GEO,IGSO,MEO "
                 << "(got '" << cfg.bdsOrbitRatio << "')\n";
            return 2;
        }
        if (a < 0 || b < 0 || c < 0) {
            cerr << "Error: bdsOrbitRatio cannot be negative (got '"
                 << cfg.bdsOrbitRatio << "')\n";
            return 2;
        }
        if (a + b + c <= 0) {
            cerr << "Error: bdsOrbitRatio excludes every BeiDou family\n";
            return 2;
        }
        bdsOrbitRatio = {a, b, c};
    }
    if (cfg.bdsMaxSats < 0) {
        cerr << "Error: bdsMaxSats must be >= 0 (0 means no cap)\n";
        return 2;
    }

    // A zero or negative sigma multiplier would divide the weight by zero or
    // flip its sign. Reject it before any file is opened.
    {
        struct NamedFactor { const char *name; double value; };
        const NamedFactor factors[] = {
            {"codeSigmaFactorGPS",  cfg.codeSigmaFactorGPS},
            {"codeSigmaFactorBD2",  cfg.codeSigmaFactorBD2},
            {"codeSigmaFactorBD3",  cfg.codeSigmaFactorBD3},
            {"codeSigmaFactorGEO",  cfg.codeSigmaFactorGEO},
            {"codeSigmaFactorIGSO", cfg.codeSigmaFactorIGSO},
            {"codeSigmaFactorMEO",  cfg.codeSigmaFactorMEO},
        };
        for (const NamedFactor &f : factors) {
            if (!(f.value > 0.0) || !std::isfinite(f.value)) {
                cerr << "Error: " << f.name << " must be a positive number (got "
                     << f.value << ")\n";
                return 2;
            }
        }
    }
    const CodeSigmaFactors sigmaFactors{
        cfg.codeSigmaFactorGPS, cfg.codeSigmaFactorBD2, cfg.codeSigmaFactorBD3,
        cfg.codeSigmaFactorGEO, cfg.codeSigmaFactorIGSO, cfg.codeSigmaFactorMEO};

    const bool mixedSystems = mode.codePairsBySystem.size() > 1;
    if (cfg.bdsMaxSats > 0 && !mixedSystems) {
        cerr << "Warning: bdsMaxSats only applies to gpsbds; ignored for '"
             << mode.key << "'\n";
    }

    string projectRoot = configDir.empty() ? "." : dirOf(configDir);

    string roverFile = optObs.empty()     ? resolvePath(projectRoot, cfg.obsFile)     : cfg.obsFile;
    string baseFile  = optBaseObs.empty() ? resolvePath(projectRoot, cfg.baseObsFile) : cfg.baseObsFile;
    string navFile   = optNav.empty()     ? resolvePath(projectRoot, cfg.navFile)     : cfg.navFile;
    string outDir    = optOutDir.empty()  ? resolvePath(projectRoot, cfg.outDir)      : cfg.outDir;
    string osbFile   = optOsb.empty()     ? resolvePath(projectRoot, cfg.osbFile)     : cfg.osbFile;

    if (optVerbose) {
        cout << "config    : " << (configDir.empty() ? "(defaults)" : configFile) << "\n";
        cout << "rover     : " << roverFile << "\n";
        cout << "base      : " << baseFile << "\n";
        cout << "nav       : " << navFile << "\n";
        cout << "outDir    : " << outDir << "\n";
        cout << "osb       : " << (osbFile.empty() ? "(none)" : osbFile) << "\n";
        cout << "sys       : " << mode.key << "  " << mode.label << "\n";
        cout << "stop      : " << (cfg.stopUTC.empty() ? "(end of file)" : cfg.stopUTC) << "\n";
        cout << "cutOff    : " << cfg.cutOffElevation << " deg\n";
        cout << "fix       : " << (cfg.fixAmbiguity ? "yes" : "no")
             << "  ratio threshold " << cfg.ratioThreshold << "\n";
        cout << "estimator : " << cfg.estimator << "\n";
        cout << "isb       : "
             << (cfg.estimateISB ? "yes (BDS-3 relative to BDS-2)" : "no") << "\n";
        cout << "trop      : " << (cfg.enableTrop ? "yes" : "no") << "\n";
        cout << "bds-tgd   : " << (cfg.enableBDSTGD ? "yes" : "no") << "\n";
        cout << "sigma     : GPS " << sigmaFactors.gps
             << "  BD2 " << sigmaFactors.bd2
             << "  BD3 " << sigmaFactors.bd3
             << "  GEO " << sigmaFactors.geo
             << "  IGSO " << sigmaFactors.igso
             << "  MEO " << sigmaFactors.meo << "\n";
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

    if (optDumpOrbitTypes) {
        // A diagnostic that does not need an epoch loop: the classification is
        // a property of the broadcast elements, not of the observation file.
        cout << "sat,orbitType\n";
        for (const auto &kv : navStore.bdsEphData)
            cout << kv.first.toString() << ","
                 << bdsOrbitTypeName(navStore.bdsOrbitType(kv.first)) << "\n";
        return 0;
    }

    //---------------------------------------------------------------
    // Observation types and solver setup 观测类型与解算器设置
    //---------------------------------------------------------------
    // The reader selects on the RAW three-character names; SPPUCCodePhase then
    // keys everything off the collapsed two-character pair. Both come from the
    // same RtkMode so they cannot drift apart.
    // One entry for a single-system mode, two for gpsbds. The receiver's clock
    // is already estimated per system inside SPPUCCodePhase, so the observation
    // selection is the only thing that needs to know both.
    std::map<string, std::set<string>> selectedTypes = mode.rawTypesBySystem;
    std::map<string, std::vector<std::pair<string, string>>> dualCodeTypes =
        mode.codePairsBySystem;

    RinexObsReader readObsRover;
    readObsRover.setFileStream(&roverObsStream);
    readObsRover.setSelectedTypes(selectedTypes);

    RinexObsReader readObsBase;
    readObsBase.setFileStream(&baseObsStream);
    readObsBase.setSelectedTypes(selectedTypes);

    // Orbit/clock source for the two single-point solves. Broadcast by
    // default; with --sp3 + --clk the identical solver runs on precise
    // products, which is the only way to separate "this SPP cannot do better"
    // from "the broadcast ephemeris is what it is". SP3Store carries its own
    // RINEX clock reader, and its relativity flag must be OFF here: the precise
    // clock already contains that term.
    SP3Store sp3Store;
    if (!optSp3.empty() || !optClk.empty()) {
        if (optSp3.empty() || optClk.empty()) {
            cerr << "Error: --sp3 and --clk must be given together\n";
            return 2;
        }
        try {
            sp3Store.loadSP3File(optSp3);
            sp3Store.loadRinexClockFile(optClk);
        } catch (const std::exception &e) {
            cerr << "Error: cannot load precise products\n"
                 << "       " << e.what() << "\n";
            return 1;
        }
        // 精密钟差产品与广播多项式同一约定（都不含相对论项），所以这里不能关——
        // 关掉等于每颗星少算 ±2~7.5 m。实测见 bds_eph 的 nav-sp3 钟差差：只有 1.9/10.3/-2.8 ns。
        sp3Store.addRelativity = true;
        cout << "orbit/clock: precise (SP3 + CLK)\n"
             << "  sp3 : " << optSp3 << "\n"
             << "  clk : " << optClk << "\n";
    }

    // Per-signal code biases. They are only accepted with precise products
    // (checked above), and they must come from the same analysis center and day
    // as those products: the OSB values are referred to that center's clock
    // datum. The coverage check below is what catches the GFZ MGEX OSB file,
    // which carries C2I/C6I for BeiDou but neither C7I nor C5P - the two signals
    // the BeiDou modes actually observe.
    SinexBiasStore osbStore;
    const bool haveOsb = !osbFile.empty();
    if (haveOsb) {
        try {
            osbStore.loadFile(osbFile);
        } catch (const std::exception &e) {
            cerr << "Error: cannot load SINEX-BIAS/OSB file: " << osbFile << "\n"
                 << "       " << e.what() << "\n";
            return 1;
        }

        std::vector<string> missing;
        for (const auto &sysTypes : mode.rawTypesBySystem) {
            for (const string &type : sysTypes.second) {
                if (type.empty() || type[0] != 'C') continue;
                if (!osbStore.hasObsType(sysTypes.first, type))
                    missing.push_back(sysTypes.first + " " + type);
            }
        }
        if (!missing.empty()) {
            cerr << "Error: the OSB file does not cover the code signals of --sys "
                 << mode.key << ":";
            for (const string &m : missing) cerr << " " << m;
            cerr << "\n       Use an orbit/clock/bias set from the same analysis\n"
                 << "       center and the same day. GFZ's MGEX OSB file, for one,\n"
                 << "       has no BeiDou C7I or C5P records.\n";
            return 1;
        }

        // A heuristic, not a validation: the product filenames start with the
        // analysis center's three-letter code (WUM, GBM, COD...). Mixing them is
        // the failure mode this option is easiest to get wrong, so say so.
        auto acPrefix = [](const string &path) -> string {
            if (path.empty()) return string();
            string name = fileNameOf(path);
            for (char &c : name) c = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(c)));
            return name.size() >= 3 ? name.substr(0, 3) : name;
        };
        const string acSp3 = acPrefix(optSp3);
        const string acClk = acPrefix(optClk);
        const string acOsb = acPrefix(osbFile);
        if (acSp3 != acClk || acSp3 != acOsb) {
            cerr << "Warning: --sp3 (" << acSp3 << "), --clk (" << acClk
                 << ") and --osb (" << acOsb
                 << ") look like different analysis centers. The bias file is\n"
                 << "         tied to its own center's clock datum; mixing them\n"
                 << "         usually makes the result worse, not better.\n";
        }
        if (cfg.enableBDSTGD) {
            cout << "  note: --bds-tgd is ignored while an OSB file is in use;\n"
                 << "        the OSB corrects to the precise clock datum instead\n";
        }

        cout << "osb        : " << osbFile << "\n"
             << "  mode " << osbStore.getBiasMode()
             << ", time system " << osbStore.getTimeSystemCode()
             << ", " << osbStore.getCodeIntervalCount() << " code intervals, "
             << osbStore.getSatelliteCount() << " satellites\n";
        const auto &clockRefs = osbStore.getClockReferences();
        const auto itC = clockRefs.find("C");
        if (itC != clockRefs.end() && !itC->second.empty()) {
            cout << "  clock ref C :";
            for (const string &t : itC->second) cout << " " << t;
            cout << "\n";
        }
    }

    SPPUCCodePhase sppRover;
    if (!optSp3.empty()) {
        sppRover.setEphStore(&sp3Store);
    } else {
        sppRover.setRinexNavStore(&navStore);
    }
    if (haveOsb) sppRover.setBiasStore(&osbStore);
    sppRover.setDualCodeTypes(dualCodeTypes);
    sppRover.setCutOffElev(cfg.cutOffElevation);
    sppRover.setEstimateISB(cfg.estimateISB);
    // Set explicitly, on BOTH receivers. This is not a formality: SPPIFCode's
    // constructor initialises both flags to TRUE and SPPUCCodePhase's own
    // constructor does not reset them. Left alone they would turn the
    // corrections on and move the byte-anchored rtk: column.
    sppRover.setTropEnable(cfg.enableTrop);
    sppRover.setEstimateTrop(optEstTrop);
    sppRover.setBDSTGDEnable(cfg.enableBDSTGD);

    SPPUCCodePhase sppBase;
    if (!optSp3.empty()) {
        sppBase.setEphStore(&sp3Store);
    } else {
        sppBase.setRinexNavStore(&navStore);
    }
    // The same biases on both receivers. Station differencing cancels them, and
    // that cancellation is exactly why the rtk: column must not move.
    if (haveOsb) sppBase.setBiasStore(&osbStore);
    sppBase.setStationAsBase();
    sppBase.setDualCodeTypes(dualCodeTypes);
    sppBase.setCutOffElev(cfg.cutOffElevation);
    // Set on the base too, though it cannot matter: the base is linearized at
    // its known position and never solved, and differenceStation() carries only
    // the rover's coefficients across. It is set so the two stations are
    // configured identically, which is easier to reason about than an asymmetry
    // that happens to be harmless.
    sppBase.setEstimateISB(cfg.estimateISB);
    // The corrections, unlike the bias above, DO have to be set on both sides:
    // the base's prefit is what the rover's is differenced against, so a term
    // modelled on one side only would not cancel. On a zero baseline the two
    // receivers share an antenna, and modelling both is what makes the
    // troposphere cancel - the behaviour to want, not an accident.
    sppBase.setTropEnable(cfg.enableTrop);
    sppBase.setEstimateTrop(optEstTrop);
    sppBase.setBDSTGDEnable(cfg.enableBDSTGD);

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
        for (const auto &sysPairs : mode.codePairsBySystem) {
            for (const auto &cp : sysPairs.second) {
                // "C2" -> "L2", "C7" -> "L7": the phase type is the code type
                // with the leading C swapped for an L, the same rule
                // SPPUCCodePhase uses.
                bands[sysPairs.first].push_back({"L" + cp.first.substr(1),
                                                 "L" + cp.second.substr(1)});
            }
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
                  "nAmb,ratio,fixed,absDxyzFixed,isb,"
                  // The same conditioning numbers for the ROVER's own
                  // single-point system, before any differencing. There are two
                  // reasons they are here. First, the DD columns above describe
                  // the double-difference system, which is not where the spp:
                  // column's error comes from. Second, and the reason this was
                  // added: whether the troposphere correction helps the spp:
                  // column (it should, by decoupling the zenith coordinate from
                  // the receiver clock) or whether nothing helps it (which would
                  // mean the system is ill-conditioned and the cure is
                  // constraints, not a model) is decided by whether sppCond
                  // moves. See docs/rtk.md.
                  "sppNObs,sppNUnk,sppRank,sppCond,"
                  // Mixed-solution columns. `datumSats` lists every reference
                  // satellite (G and/or C), while the legacy `datumSat` above
                  // stays the GPS one when there is a GPS block. The counts are
                  // the BeiDou candidates seen and the ones the quota kept;
                  // both are zero for a single-system run.
                  "datumSats,bdsCandGEO,bdsCandIGSO,bdsCandMEO,"
                  "bdsSelGEO,bdsSelIGSO,bdsSelMEO\n";
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

    // One smoother serves both stations: its state is keyed by station name.
    // The slipped sets carry the previous epoch's cycle-slip flags into it.
    CarrierSmoother smoother;
    std::set<SatID> slippedRoverPrev, slippedBasePrev;
    if (optSmooth > 0) {
        smoother.setWindow(optSmooth);
        cout << "carrier smoothing: Hatch filter, window " << optSmooth
             << " epochs\n";
    }

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
        // Hatch-smooth the code with its carrier first, when asked. This is
        // what a receiver's own firmware does and what its reported position
        // reflects; without it the two disagree by everything the site adds to
        // the code, which the double difference then cancels silently.
        //
        // The reset signal is chapter 7's slip flags. They are computed at the
        // bottom of this loop, not here, because detect() rewrites the
        // observations it is handed - it drops the satellites it cannot form a
        // combination for - and running it before the solve would take those
        // satellites out of the single-point equations too. So the smoother
        // gets the PREVIOUS epoch's flags: one epoch of delay, which is all a
        // reset needs.
        if (optSmooth > 0) {
            smoother.smooth(roverData, &slippedRoverPrev);
        }
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
        // Per-system / per-orbit code weighting. Applied to the copy the
        // double-difference pipeline uses; the single-point solve above has
        // already run at the built-in sigma.
        applyCodeSigmaFactors(equSysRover, sigmaFactors, navStore);

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

        if (!optBaseXyz.empty()) {
            // The datum the double difference resolves against. Setting it on
            // the observation is enough: sppBase is linearized here and never
            // solved, so this is the only place the position is read.
            double bx, by, bz;
            if (sscanf(optBaseXyz.c_str(), "%lf,%lf,%lf", &bx, &by, &bz) == 3) {
                baseData.antennaPosition = Eigen::Vector3d(bx, by, bz);
            } else {
                cerr << "Error: --base-xyz wants X,Y,Z in metres\n";
                return 2;
            }
        }
        if (optSmooth > 0) {
            smoother.smooth(baseData, &slippedBasePrev);
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
        applyCodeSigmaFactors(equSysBase, sigmaFactors, navStore);

        // --- cycle-slip flags (--dump-cs) ------------------------------------
        // Detected AFTER both single-point solves. That order is the lecture
        // notes' own and it is load-bearing: the detectors drop the satellites
        // they cannot form a combination for, so running them first would take
        // those satellites out of the equations as well.
        VariableDataMap csFlagRover, csFlagBase, csFlagSd;
        if (wantCsFlags || optSmooth > 0) {
            csFlagRover = csDetRover.detect(roverData);
            csFlagBase  = csDetBase.detect(baseData);
            // Hand the satellites that slipped to the next epoch's smoother.
            // Without --dump-cs these flags are computed and not reported, but
            // the detection itself is what the smoothing needs; the flagged
            // difference stage below stays guarded, so a run that wants no
            // flags still gets the unflagged double difference.
            slippedRoverPrev.clear();
            for (const auto &kv : csFlagRover) slippedRoverPrev.insert(kv.first.sat);
            slippedBasePrev.clear();
            for (const auto &kv : csFlagBase) slippedBasePrev.insert(kv.first.sat);
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

        // --- BeiDou orbital-family quota (mixed solution only) ----------------
        // GPS contributes every satellite it has; the cap applies to BeiDou,
        // where a stationary GEO is not worth the same as a MEO. The selection
        // is made on the STATION-DIFFERENCED satellites, i.e. on the ones that
        // both receivers actually tracked.
        BdsOrbitCounts orbitCounts;
        if (mixedSystems) {
            set<SatID> keep;
            for (const SatID &s : sdSats)
                if (s.system != "C") keep.insert(s);

            std::vector<SatID> picked = selectBdsByOrbit(
                sdSats, sppRover.getSatElevData(), navStore,
                bdsOrbitRatio, cfg.bdsMaxSats, orbitCounts);
            for (const SatID &s : picked) keep.insert(s);

            keepOnlySats(equSysSD, keep);
            sdSats.clear();
            for (const auto &oe : equSysSD.obsEquData)
                sdSats.insert(oe.first.sat);

            // A constellation with fewer than two satellites cannot form a
            // between-satellite difference at all. In a mixed solution that
            // makes the epoch a single-system one, which is not what this mode
            // promises - skip it and say so in the skip counts.
            std::map<string, int> perSystem;
            for (const SatID &s : sdSats) ++perSystem[s.system];
            bool enoughForAll = true;
            for (const auto &kv : mode.codePairsBySystem)
                if (perSystem[kv.first] < 2) enoughForAll = false;
            if (!enoughForAll) {
                ++skipNoSd;
                if (optVerbose)
                    cout << "[skip] sod " << sod
                         << " fewer than two common satellites in a system ("
                         << perSystem["G"] << "G " << perSystem["C"] << "C)\n";
                continue;
            }
        }

        // --- between satellites ----------------------------------------------
        bool datumFallback = false;
        std::map<string, SatID> datumSats =
            pickDatumSats(equSysSD, sppRover.getSatElevData(), datumFallback);
        if (datumFallback) ++skipDatumFallback;
        if (datumSats.empty()) {
            ++skipNoDd;
            continue;
        }

        // The diagnostics keep one `datumSat` column for backward
        // compatibility; a mixed solution reports the GPS one there and every
        // reference in the extra `datumSats` column.
        SatID datumSat = datumSats.begin()->second;
        {
            auto git = datumSats.find("G");
            if (git != datumSats.end()) datumSat = git->second;
        }
        string datumSatsText;
        for (const auto &kv : datumSats) {
            if (!datumSatsText.empty()) datumSatsText += ";";
            datumSatsText += kv.second.toString();
        }

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
            /*
            - 卡尔曼走五参数重载——它保留基准星自己的模糊度（系数 −λ），所以模糊度变量是站间单差模糊度，意义不依赖基准星是哪颗。
              滤波器必须跨历元传这些量，而基准星一次运行里要换两三次。代价是秩亏 1，得靠教材 (8.3.4.3) 的约束方程补上（就是
              ambiguityDatum()，必须在求解前加）；
            - 最小二乘走三参数重载，把基准星模糊度直接差掉；
            */
            differenceSat(datumSats, equSysSD, csFlagSd, equSysDD, csFlagDd,
                          optDdCov);
        } else {
            differenceSat(datumSats, equSysSD, equSysDD, optDdCov);
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
                ambiguityDatum(kalmanFirstEpoch, datumSats, kalmanFixedAmb, equSysDD);
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
        // The rover's single-point system, as it was linearized (getEquSys()
        // hands back a copy, and differenceStation only reads it). Passing an
        // empty state leaves sigma0/postfitRms at 0 - rank and cond need the
        // design matrix alone, which is all this asks of it.
        DdDiag sppDiag = diagnoseDd(equSysRover, VectorXd());

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
                   << stateValue(equSysDD, stateVec, Parameter::ifb) << ","
                   << sppDiag.nObs << ","
                   << sppDiag.nUnk << ","
                   << sppDiag.rank << ","
                   << sppDiag.cond << ","
                   << datumSatsText << ","
                   << orbitCounts.candidate[0] << ","
                   << orbitCounts.candidate[1] << ","
                   << orbitCounts.candidate[2] << ","
                   << orbitCounts.selected[0] << ","
                   << orbitCounts.selected[1] << ","
                   << orbitCounts.selected[2] << "\n";

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
        // The precise products and their bias file decide the spp: column and
        // were never recorded before. An empty string means "not used".
        mf << "  \"sp3\": \"" << jsonEscape(optSp3) << "\",\n";
        mf << "  \"clk\": \"" << jsonEscape(optClk) << "\",\n";
        mf << "  \"osb\": \"" << jsonEscape(haveOsb ? osbFile : string()) << "\",\n";
        mf << "  \"osbMode\": \""
           << jsonEscape(haveOsb ? osbStore.getBiasMode() : string()) << "\",\n";
        mf << "  \"osbTimeSystem\": \""
           << jsonEscape(haveOsb ? osbStore.getTimeSystemCode() : string()) << "\",\n";
        mf << "  \"osbCodeIntervals\": "
           << (haveOsb ? osbStore.getCodeIntervalCount() : 0) << ",\n";
        mf << "  \"osbSatellites\": "
           << (haveOsb ? osbStore.getSatelliteCount() : 0) << ",\n";
        mf << "  \"osbSkippedDsb\": "
           << (haveOsb ? osbStore.getDsbRecordCount() : 0) << ",\n";
        mf << "  \"osbSkippedStation\": "
           << (haveOsb ? osbStore.getStationRecordCount() : 0) << ",\n";
        mf << "  \"osbCodeCorrections\": "
           << sppRover.getCodeBiasCorrections() << ",\n";
        mf << "  \"osbMissingCodeBiases\": "
           << sppRover.getCodeBiasMissing() << ",\n";
        mf << "  \"sys\": \"" << jsonEscape(mode.key) << "\",\n";
        mf << "  \"sysLabel\": \"" << jsonEscape(mode.label) << "\",\n";
        mf << "  \"estimator\": \"" << jsonEscape(cfg.estimator) << "\",\n";
        mf << "  \"isbEstimated\": " << (cfg.estimateISB ? "true" : "false") << ",\n";
        // The two single-point corrections. Recorded because they move the spp:
        // column and, through rtk: = xyzRover + dxyzRTK, can move the rtk: one -
        // so a run's own numbers are not comparable to another's without them.
        mf << "  \"tropEnabled\": " << (cfg.enableTrop ? "true" : "false") << ",\n";
        // Exercise 3: whether the double differences were weighted with their
        // full covariance. Two runs of the same tree differ only in this.
        mf << "  \"ddCov\": " << (optDdCov ? "true" : "false") << ",\n";
        mf << "  \"bdsTgdEnabled\": " << (cfg.enableBDSTGD ? "true" : "false") << ",\n";
        // A list, because a merged mode has one pair per generation (and
        // gpsbds one per constellation). The single-generation modes still
        // write exactly one.
        mf << "  \"codePairs\": [";
        {
            bool first = true;
            for (const auto &sysPairs : mode.codePairsBySystem) {
                for (const auto &cp : sysPairs.second) {
                    mf << (first ? "" : ", ") << "[\"" << jsonEscape(cp.first)
                       << "\", \"" << jsonEscape(cp.second) << "\"]";
                    first = false;
                }
            }
        }
        mf << "],\n";
        // The same information keyed by constellation, which is what a mixed
        // run needs to be reproducible: the flat list above cannot say whether
        // C2/C7 belonged to GPS or BeiDou.
        mf << "  \"systems\": [";
        {
            bool first = true;
            for (const auto &kv : mode.codePairsBySystem) {
                mf << (first ? "" : ", ") << "\"" << jsonEscape(kv.first) << "\"";
                first = false;
            }
        }
        mf << "],\n";
        mf << "  \"codePairsBySystem\": {";
        {
            bool firstSys = true;
            for (const auto &sysPairs : mode.codePairsBySystem) {
                mf << (firstSys ? "" : ", ") << "\"" << jsonEscape(sysPairs.first)
                   << "\": [";
                for (size_t k = 0; k < sysPairs.second.size(); ++k) {
                    mf << (k ? ", " : "")
                       << "[\"" << jsonEscape(sysPairs.second[k].first)
                       << "\", \"" << jsonEscape(sysPairs.second[k].second) << "\"]";
                }
                mf << "]";
                firstSys = false;
            }
        }
        mf << "},\n";
        mf << "  \"bdsOrbitRatio\": [" << bdsOrbitRatio[0] << ", "
           << bdsOrbitRatio[1] << ", " << bdsOrbitRatio[2] << "],\n";
        mf << "  \"bdsMaxSats\": " << cfg.bdsMaxSats << ",\n";
        mf << "  \"codeSigmaFactors\": {\"GPS\": " << sigmaFactors.gps
           << ", \"BD2\": " << sigmaFactors.bd2
           << ", \"BD3\": " << sigmaFactors.bd3
           << ", \"GEO\": " << sigmaFactors.geo
           << ", \"IGSO\": " << sigmaFactors.igso
           << ", \"MEO\": " << sigmaFactors.meo << "},\n";
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
