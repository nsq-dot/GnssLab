/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Between-station and between-satellite differencing, on paper and on screen.
 *
 * Textbook chapter 8, sections 8.3.1-8.3.2. `differenceStation()` and
 * `differenceSat()` are the two routines that turn two receivers' linearized
 * equation systems into a double-difference system, and they are small enough to
 * be read in one sitting - but the way they are written hides three things that
 * are worth seeing happen:
 *
 *   - `differenceStation()` deletes every `Parameter::iono` coefficient. It does
 *     not difference the ionospheric delay, it drops the parameter entirely,
 *     which is the short-baseline assumption (dI -> 0) taken literally.
 *   - the double differences cancel the receiver clock, so `Parameter::cdt` does
 *     not appear in the result at all.
 *   - the ambiguity coefficient is copied through UNCHANGED. The variable is
 *     still named after satellite i, but the number it holds is the DD
 *     ambiguity, i.e. N_i - N_datum. Nothing in the data structure says so. This
 *     is the part that trips people up, so the output labels it explicitly.
 *
 * It also prints the rank of the double-difference design matrix, because rank
 * deficiency is the failure a student meets first on real data and the least
 * visible: it does not raise, it produces a plausible-looking wrong answer.
 *
 * Usage:
 *   diff_station                # run the two built-in examples
 *   diff_station --demo         # same
 *   diff_station < system.txt   # analyse an equation system from stdin
 *
 * Input format (whitespace separated, `#` starts a comment):
 *
 *   rover
 *   <sat> <type> <prefit> <weight> [<coef>=<value>]...
 *   ...
 *   base
 *   <sat> <type> <prefit> <weight>
 *   ...
 *   datum <sat>            # optional; defaults to the first satellite
 *
 * The rover's coefficients may be `dx`, `dy`, `dz`, `cdt`, `iono` or `amb`.
 * Base lines need only the residual and weight: `differenceStation()` takes the
 * base equation's prefit and weight and nothing else from it.
 */

#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <unistd.h>   // isatty

#include <Eigen/Dense>

#include "GnssStruct.h"
#include "GnssFunc.h"
#include "CoordStruct.h"

using namespace std;
using namespace Eigen;

namespace {

struct Coef {
    string name;
    double value;
};

struct Row {
    string sat;
    string type;
    double prefit = 0.0;
    double weight = 1.0;
    vector<Coef> coefs;
};

/// Build the Variable a coefficient name refers to, for this station.
Variable makeVariable(const string &station, const SatID &sat, const string &type,
                      const string &coefName) {
    if (coefName == "dx") return Variable(station, Parameter::dX);
    if (coefName == "dy") return Variable(station, Parameter::dY);
    if (coefName == "dz") return Variable(station, Parameter::dZ);
    if (coefName == "cdt") return Variable(station, Parameter::cdt);
    if (coefName == "cdtBDS") return Variable(station, Parameter::cdtBDS);
    if (coefName == "iono") return Variable(station, sat, Parameter::iono, ObsID(sat.system, type.substr(0, 2)));
    if (coefName == "amb") return Variable(station, sat, Parameter::ambiguity, ObsID(sat.system, type));
    throw std::runtime_error("unknown coefficient name '" + coefName + "'");
}

EquSys buildEquSys(const vector<Row> &rows, const string &station) {
    EquSys es;
    es.station = station;
    for (const Row &r : rows) {
        SatID sat(r.sat);
        EquID id(sat, r.type);
        es.obsEquData[id].prefit = r.prefit;
        es.obsEquData[id].weight = r.weight;
        for (const Coef &c : r.coefs) {
            Variable v = makeVariable(station, sat, r.type, c.name);
            es.obsEquData[id].varCoeffData[v] = c.value;
            es.varSet.insert(v);
        }
    }
    return es;
}

/// Dense design matrix in varSet order, plus the residual and weight vectors.
void denseForm(const EquSys &es, MatrixXd &H, VectorXd &pre, VectorXd &wgt) {
    H = MatrixXd::Zero(es.obsEquData.size(), es.varSet.size());
    pre = VectorXd::Zero(es.obsEquData.size());
    wgt = VectorXd::Zero(es.obsEquData.size());

    int i = 0;
    for (const auto &oe : es.obsEquData) {
        pre(i) = oe.second.prefit;
        wgt(i) = oe.second.weight;
        for (const auto &vc : oe.second.varCoeffData) {
            auto it = es.varSet.find(vc.first);
            if (it == es.varSet.end()) continue;
            H(i, (int) std::distance(es.varSet.begin(), it)) = vc.second;
        }
        ++i;
    }
}

const char *paraName(int i) {
    static const char *n[] = {"Unknown", "dX", "dY", "dZ", "cdt", "cdtBDS", "dVX", "dVY",
                              "dVZ", "cdtDot", "cdtDotBDS", "ifb", "iono", "ambiguity"};
    return (i >= 0 && i <= 13) ? n[i] : "?";
}

void printVarSet(const EquSys &es, const char *what) {
    map<int, int> hist;
    for (const Variable &v : es.varSet) hist[(int) v.getParaType()]++;

    cout << "  " << what << " unknown set: " << es.varSet.size() << " parameter(s) -";
    for (const auto &h : hist) cout << " " << paraName(h.first) << "=" << h.second;
    cout << "\n";
}

/// One full analysis: station difference, satellite difference, rank.
void analyse(const char *title,
             const vector<Row> &roverRows,
             const vector<Row> &baseRows,
             const string &datumSatName) {
    cout << "\n";
    cout << "================================================================\n";
    cout << title << "\n";
    cout << "================================================================\n";

    EquSys rover = buildEquSys(roverRows, "ROVER");
    EquSys base  = buildEquSys(baseRows,  "BASE");

    cout << "\n-- input -------------------------------------------------------\n";
    cout << "  rover: " << rover.obsEquData.size() << " equation(s)\n";
    printVarSet(rover, "rover");
    cout << "  base : " << base.obsEquData.size() << " equation(s)\n";

    //---------------------------------------------------------------
    // Between stations
    //---------------------------------------------------------------
    EquSys sd;
    differenceStation(rover, base, sd);

    cout << "\n-- differenceStation() -----------------------------------------\n";
    cout << "  " << sd.obsEquData.size() << " single-difference equation(s)\n";
    printVarSet(sd, "single-difference");

    cout << "\n  Each residual is rover - base, and each weight is the harmonic\n"
            "  combination  1 / (1/w_rover + 1/w_base):\n\n";
    cout << "    " << left << setw(6) << "sat" << setw(5) << "type"
         << right << setw(14) << "rover" << setw(14) << "base" << setw(14) << "SD"
         << setw(12) << "w_rover" << setw(12) << "w_base" << setw(12) << "w_SD" << "\n";
    for (const auto &oe : sd.obsEquData) {
        double rp = rover.obsEquData.at(oe.first).prefit;
        double bp = base.obsEquData.at(oe.first).prefit;
        double rw = rover.obsEquData.at(oe.first).weight;
        double bw = base.obsEquData.at(oe.first).weight;
        cout << "    " << left << setw(6) << oe.first.sat.toString() << setw(5) << oe.first.obsType
             << right << fixed << setprecision(4)
             << setw(14) << rp << setw(14) << bp << setw(14) << oe.second.prefit
             << setw(12) << rw << setw(12) << bw << setw(12) << oe.second.weight << "\n";
    }

    bool ionoGone = true;
    for (const Variable &v : sd.varSet)
        if (v.getParaType() == Parameter::iono) ionoGone = false;
    cout << "\n  Parameter::iono in the single-difference set: "
         << (ionoGone ? "gone - short-baseline assumption, not a difference of delays"
                      : "PRESENT (unexpected)") << "\n";

    //---------------------------------------------------------------
    // Between satellites
    //---------------------------------------------------------------
    SatID datum(datumSatName);
    EquSys dd;
    differenceSat(datum, sd, dd);

    cout << "\n-- differenceSat(datum = " << datum.toString() << ") ----------------------------\n";
    cout << "  " << dd.obsEquData.size() << " double-difference equation(s)\n";
    printVarSet(dd, "double-difference");

    cout << "\n    " << left << setw(6) << "sat" << setw(5) << "type"
         << right << setw(14) << "prefit" << setw(12) << "weight" << "   coefficients\n";
    for (const auto &oe : dd.obsEquData) {
        cout << "    " << left << setw(6) << oe.first.sat.toString() << setw(5) << oe.first.obsType
             << right << fixed << setprecision(4)
             << setw(14) << oe.second.prefit << setw(12) << oe.second.weight << "   ";
        for (const auto &vc : oe.second.varCoeffData) {
            int t = (int) vc.first.getParaType();
            cout << paraName(t);
            if (t == (int) Parameter::ambiguity)
                cout << "[" << vc.first.getSat().toString() << "]";
            cout << "=" << vc.second << " ";
        }
        cout << "\n";
    }

    bool cdtGone = true;
    for (const Variable &v : dd.varSet)
        if (v.getParaType() == Parameter::cdt || v.getParaType() == Parameter::cdtBDS) cdtGone = false;
    cout << "\n  Receiver clock in the double-difference set: "
         << (cdtGone ? "gone - cancelled by differencing two satellites" : "PRESENT (unexpected)") << "\n";
    cout << "  Ambiguity coefficients are copied through UNCHANGED: the variable\n"
            "  named after each satellite holds the DD ambiguity N_i - N_datum.\n";

    //---------------------------------------------------------------
    // Rank
    //---------------------------------------------------------------
    if (dd.obsEquData.empty() || dd.varSet.empty()) {
        cout << "\n  No equations survived - nothing to rank.\n";
        return;
    }

    MatrixXd H;
    VectorXd pre, wgt;
    denseForm(dd, H, pre, wgt);

    FullPivLU<MatrixXd> lu(H);
    int rank = (int) lu.rank();
    int nObs = (int) H.rows(), nUnk = (int) H.cols();

    cout << "\n-- conditioning ------------------------------------------------\n";
    cout << "  observations " << nObs << ", unknowns " << nUnk
         << ", rank " << rank << "\n";
    cout << "  degrees of freedom nObs - nUnk = " << (nObs - nUnk) << "\n";

    JacobiSVD<MatrixXd> svd(H);
    if (svd.singularValues().size() > 0) {
        double smax = svd.singularValues()(0);
        double smin = svd.singularValues()(svd.singularValues().size() - 1);
        cout << "  condition number " << (smin > 0.0 ? smax / smin : 0.0) << "\n";
    }

    if (rank < nUnk) {
        cout << "\n  ** RANK DEFICIENT: " << (nUnk - rank)
             << " direction(s) in which the observations say nothing. \n"
             "     SolverLSQ inverts the normal matrix here, so this does not raise -\n"
             "     it returns whatever the rounding produces. Drop the datum satellite's\n"
             "     equations, or give two satellites the same geometry, and this is what\n"
             "     you get. **\n";
    } else {
        cout << "  Full column rank: every unknown is determined.\n";
    }

    // Machine-readable summary, for tests/test_rtk_equations.py. One line so the
    // test does not have to parse the tables above, which are written to be read
    // by a person and may be reworded.
    int nAmb = 0, nIonoSd = 0, nClockDd = 0;
    for (const Variable &v : dd.varSet)
        if (v.getParaType() == Parameter::ambiguity) ++nAmb;
    for (const Variable &v : sd.varSet)
        if (v.getParaType() == Parameter::iono) ++nIonoSd;
    for (const Variable &v : dd.varSet)
        if (v.getParaType() == Parameter::cdt || v.getParaType() == Parameter::cdtBDS) ++nClockDd;

    cout << "\n  SUMMARY nObs=" << nObs << " nUnk=" << nUnk << " rank=" << rank
         << " dof=" << (nObs - nUnk)
         << " ambiguities=" << nAmb
         << " ionoInSD=" << nIonoSd
         << " clockInDD=" << nClockDd
         << " datum=" << datum.toString() << "\n";
}

//-------------------------------------------------------------------
// Built-in fixtures
//-------------------------------------------------------------------
// Four satellites, four observation types each, one of which is the datum.
//
// Four, not three: with one datum and only two others, the double differences
// span just two directions in space, so the coordinate is determined only within
// the plane those two contain and the system is rank deficient by one however
// well the satellites are spread. A single-epoch double difference needs three
// non-datum satellites before it can pin a position in three dimensions. (This
// is the same counting that makes a single-point solution need four.) The first
// version of this example used three and reported its own "well-posed" case as
// rank deficient - a useful reminder that the rank check is worth printing.
//
// The geometry is the only thing that changes between the two examples.
const char *WELL_POSED = R"(
rover
# G08 is the datum: its coefficients are the reference every other satellite is
# differenced against.
G08 C1   6.000 11.111 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=1.0
G08 C2   6.400 11.111 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=1.647
G08 L1   5.900 55555.5 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=-1.0  amb=0.190
G08 L2   6.300 55555.5 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=-1.647 amb=0.244
# G21, G27 and G30 look in three different directions, so their double
# differences carry three independent pieces of geometry.
G21 C1  -4.000 11.111 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=1.0
G21 C2  -4.200 11.111 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=1.647
G21 L1  -3.800 55555.5 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=-1.0  amb=0.190
G21 L2  -4.100 55555.5 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=-1.647 amb=0.244
G27 C1   2.500 11.111 dx=-0.150 dy=-0.685 dz=-0.713 cdt=1.0 iono=1.0
G27 C2   2.700 11.111 dx=-0.150 dy=-0.685 dz=-0.713 cdt=1.0 iono=1.647
G27 L1   2.400 55555.5 dx=-0.150 dy=-0.685 dz=-0.713 cdt=1.0 iono=-1.0  amb=0.190
G27 L2   2.600 55555.5 dx=-0.150 dy=-0.685 dz=-0.713 cdt=1.0 iono=-1.647 amb=0.244
G30 C1  -1.500 11.111 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=1.0
G30 C2  -1.700 11.111 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=1.647
G30 L1  -1.400 55555.5 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=-1.0  amb=0.190
G30 L2  -1.600 55555.5 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=-1.647 amb=0.244
base
G08 C1   5.900 11.111
G08 C2   6.300 11.111
G08 L1   5.800 55555.5
G08 L2   6.200 55555.5
G21 C1  -4.050 11.111
G21 C2  -4.250 11.111
G21 L1  -3.850 55555.5
G21 L2  -4.150 55555.5
G27 C1   2.450 11.111
G27 C2   2.650 11.111
G27 L1   2.350 55555.5
G27 L2   2.550 55555.5
G30 C1  -1.550 11.111
G30 C2  -1.750 11.111
G30 L1  -1.450 55555.5
G30 L2  -1.650 55555.5
datum G08
)";

// Same four satellites, but G27 is given G21's line-of-sight vector, so those
// two double differences are proportional and the geometry collapses from three
// directions to two.
const char *RANK_DEFICIENT = R"(
rover
G08 C1   6.000 11.111 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=1.0
G08 C2   6.400 11.111 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=1.647
G08 L1   5.900 55555.5 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=-1.0  amb=0.190
G08 L2   6.300 55555.5 dx=0.364 dy=-0.414 dz=-0.834 cdt=1.0 iono=-1.647 amb=0.244
G21 C1  -4.000 11.111 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=1.0
G21 C2  -4.200 11.111 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=1.647
G21 L1  -3.800 55555.5 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=-1.0  amb=0.190
G21 L2  -4.100 55555.5 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=-1.647 amb=0.244
# G27 copies G21's line of sight exactly.
G27 C1   2.500 11.111 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=1.0
G27 C2   2.700 11.111 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=1.647
G27 L1   2.400 55555.5 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=-1.0  amb=0.190
G27 L2   2.600 55555.5 dx=0.742 dy=-0.660 dz=-0.118 cdt=1.0 iono=-1.647 amb=0.244
G30 C1  -1.500 11.111 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=1.0
G30 C2  -1.700 11.111 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=1.647
G30 L1  -1.400 55555.5 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=-1.0  amb=0.190
G30 L2  -1.600 55555.5 dx=0.946 dy=0.194 dz=-0.258 cdt=1.0 iono=-1.647 amb=0.244
base
G08 C1   5.900 11.111
G08 C2   6.300 11.111
G08 L1   5.800 55555.5
G08 L2   6.200 55555.5
G21 C1  -4.050 11.111
G21 C2  -4.250 11.111
G21 L1  -3.850 55555.5
G21 L2  -4.150 55555.5
G27 C1   2.450 11.111
G27 C2   2.650 11.111
G27 L1   2.350 55555.5
G27 L2   2.550 55555.5
G30 C1  -1.550 11.111
G30 C2  -1.750 11.111
G30 L1  -1.450 55555.5
G30 L2  -1.650 55555.5
datum G08
)";

/// Parse "sat type prefit weight [name=value]..." into `rows`.
bool parseLine(const string &line, vector<Row> &rows, string &err) {
    istringstream is(line);
    Row r;
    if (!(is >> r.sat >> r.type)) return true;   // blank line
    if (!(is >> r.prefit)) { err = "missing prefit: " + line; return false; }
    if (!(is >> r.weight)) { err = "missing weight: " + line; return false; }

    string tok;
    while (is >> tok) {
        size_t eq = tok.find('=');
        if (eq == string::npos) { err = "coefficient is not name=value: " + tok; return false; }
        Coef c;
        c.name = tok.substr(0, eq);
        try { c.value = std::stod(tok.substr(eq + 1)); }
        catch (...) { err = "coefficient value is not a number: " + tok; return false; }
        r.coefs.push_back(c);
    }
    rows.push_back(r);
    return true;
}

/// Read a whole stdin document. Returns false if it has no rover section.
bool parseInput(istream &in, vector<Row> &roverRows, vector<Row> &baseRows, string &datumSat) {
    string line;
    vector<Row> *cur = nullptr;
    while (std::getline(in, line)) {
        size_t hash = line.find('#');
        if (hash != string::npos) line = line.substr(0, hash);
        istringstream probe(line);
        string first;
        if (!(probe >> first)) continue;

        if (first == "rover") { cur = &roverRows; continue; }
        if (first == "base")  { cur = &baseRows;  continue; }
        if (first == "datum") { probe >> datumSat; continue; }

        if (!cur) continue;   // table content before any section keyword
        string err;
        if (!parseLine(line, *cur, err)) {
            cerr << "Error: " << err << "\n";
            return false;
        }
    }
    return !roverRows.empty();
}

}  // namespace

int main(int argc, char *argv[]) {
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--demo") demo = true;
        else if (a == "-h" || a == "--help") {
            cout << "Usage: diff_station [--demo]\n"
                    "\n"
                    "Demonstrates differenceStation() and differenceSat() from chapter 8.\n"
                    "With no arguments it reads an equation system from stdin; --demo runs\n"
                    "two built-in examples instead (one well posed, one rank deficient).\n"
                    "See the header comment for the input format.\n";
            return 0;
        } else {
            cerr << "Error: unknown option '" << a << "'\n";
            return 2;
        }
    }

    // Running with nothing on stdin is the common case for a teaching program -
    // someone double-clicks it, or runs it through `gnss ex`. Falling back to the
    // built-in examples beats printing nothing.
    if (!demo && isatty(0)) demo = true;

    if (!demo) {
        vector<Row> roverRows, baseRows;
        string datumSat;
        if (!parseInput(cin, roverRows, baseRows, datumSat)) {
            if (!cin.eof() && cin.bad()) {
                cerr << "Error: could not read stdin\n";
                return 1;
            }
            cerr << "Note: no 'rover' section on stdin; running the built-in examples.\n"
                    "      Pass --help for the input format.\n";
            demo = true;
        } else {
            if (datumSat.empty()) datumSat = roverRows.front().sat;
            analyse("equation system from stdin", roverRows, baseRows, datumSat);
            return 0;
        }
    }

    if (demo) {
        vector<Row> roverRows, baseRows;
        string datum;

        roverRows.clear(); baseRows.clear(); datum.clear();
        istringstream a(WELL_POSED);
        parseInput(a, roverRows, baseRows, datum);
        analyse("EXAMPLE 1: well-posed double difference", roverRows, baseRows, datum);

        roverRows.clear(); baseRows.clear(); datum.clear();
        istringstream b(RANK_DEFICIENT);
        parseInput(b, roverRows, baseRows, datum);
        analyse("EXAMPLE 2: rank-deficient double difference", roverRows, baseRows, datum);
    }

    return 0;
}
