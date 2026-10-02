/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Epoch synchronisation between a rover and a base receiver.
 *
 * Textbook chapter 8, section 8.4 step (4): "从基准站的观测文件中读取与流动站时间相
 * 匹配的观测数据。若出现同步错误，则跳过基准站的处理，继续读取下一个流动站的历元数据。"
 *
 * ## What this demonstrates, and what it does not
 *
 * The production synchroniser is RinexObsReader::parseRinexObs(CommonTime&): it
 * walks a file stream forward until it finds an epoch at or after the reference,
 * and rewinds with a SyncException when the stream has overshot. That routine is
 * half stream manipulation and half decision, and only the decision is
 * interesting - so it lives here as alignEpochs() in src/EpochAlign.h, and this
 * program drives that decision over two lists of epochs.
 *
 * Being honest about the gap: this does NOT exercise the file-position handling,
 * because doing so needs two RINEX files and examples/ is documented as needing
 * no dataset. The end-to-end path is covered instead by apps/rtk, which
 * synchronises against the base file on every epoch it processes.
 *
 * Usage:
 *   sync_obs                # run the built-in example
 *   sync_obs < file         # two epoch lists, "rover" then "base"
 *
 * Input format (whitespace separated, `#` starts a comment):
 *
 *   rover
 *   <year> <doy> <sod>
 *   ...
 *   base
 *   <year> <doy> <sod>
 *   ...
 *
 * Each line is a RINEX-style year / day-of-year / second-of-day triple, which is
 * what YDSTime prints and what the solution files contain.
 */

#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unistd.h>   // isatty

#include "TimeStruct.h"
#include "TimeConvert.h"
#include "EpochAlign.h"

using namespace std;

namespace {

double sodOf(const CommonTime &t) {
    return CommonTime2YDSTime(t).sod;
}

/// Walk the base epoch list against each rover epoch, reporting every decision.
void align(const vector<CommonTime> &rover, const vector<CommonTime> &base) {
    const double tol = epochSyncTolerance();

    cout << "rover epochs: " << rover.size() << "\n";
    cout << "base  epochs: " << base.size() << "\n";
    cout << "tolerance   : " << tol << " s\n\n";

    size_t b = 0;
    int matched = 0, skipped = 0, exhausted = 0;

    cout << "  " << left << setw(12) << "rover sod" << setw(12) << "base sod"
         << "decision\n";
    cout << "  " << string(38, '-') << "\n";

    for (size_t r = 0; r < rover.size(); ++r) {
        // Advance the base stream until it reaches the rover's epoch, exactly as
        // RinexObsReader::parseRinexObs(CommonTime&) loops over the file.
        bool resolved = false;
        while (b < base.size()) {
            EpochAlign d = alignEpochs(base[b], rover[r], tol);
            if (d == EpochAlign::STREAM_BEHIND) {
                ++b;                       // not yet caught up - read the next one
                continue;
            }
            if (d == EpochAlign::STREAM_AHEAD) {
                // The base jumped past this rover epoch: no counterpart exists.
                // The production reader rewinds the stream and throws
                // SyncException, and the caller moves on to the next rover epoch.
                cout << "  " << left << setw(12) << fixed << setprecision(3) << sodOf(rover[r])
                     << setw(12) << sodOf(base[b])
                     << "SKIP rover epoch - base has already passed it "
                        "(SyncException)\n";
                ++skipped;
                resolved = true;
                break;
            }
            // MATCH
            cout << "  " << left << setw(12) << fixed << setprecision(3) << sodOf(rover[r])
                 << setw(12) << sodOf(base[b]) << "MATCH\n";
            ++matched;
            ++b;
            resolved = true;
            break;
        }
        if (!resolved) {
            ++exhausted;
        }
    }

    cout << "\n  matched " << matched
         << ", skipped " << skipped
         << ", rover epochs with no base data left " << exhausted << "\n";
}

// Two receivers, 1 Hz, over eight seconds. The base drops sod 3 and drops out
// entirely after sod 6, so the run shows a match, a skipped rover epoch, and the
// end-of-data case in one pass.
const char *BUILT_IN = R"(
rover
2022 62 24517
2022 62 24518
2022 62 24519
2022 62 24520
2022 62 24521
2022 62 24522
2022 62 24523
2022 62 24524
base
2022 62 24517
2022 62 24518
2022 62 24519
2022 62 24521
2022 62 24522
2022 62 24523
)";

bool parseInput(istream &in, vector<CommonTime> &rover, vector<CommonTime> &base) {
    string line;
    vector<CommonTime> *cur = nullptr;

    while (std::getline(in, line)) {
        size_t hash = line.find('#');
        if (hash != string::npos) line = line.substr(0, hash);

        istringstream is(line);
        string first;
        if (!(is >> first)) continue;

        if (first == "rover") { cur = &rover; continue; }
        if (first == "base")  { cur = &base;  continue; }
        if (!cur) continue;

        // `first` already holds the year - the stream is positioned after it.
        int y, doy;
        double sod;
        try {
            y = std::stoi(first);
        } catch (...) {
            cerr << "Error: expected '<year> <doy> <sod>', got: " << line << "\n";
            return false;
        }
        if (!(is >> doy >> sod)) {
            cerr << "Error: expected '<year> <doy> <sod>', got: " << line << "\n";
            return false;
        }
        YDSTime yds(y, doy, sod, TimeSystem::GPS);
        cur->push_back(YDSTime2CommonTime(yds));
    }
    return !rover.empty() && !base.empty();
}

}  // namespace

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "-h" || a == "--help") {
            cout << "Usage: sync_obs\n"
                    "\n"
                    "Demonstrates the epoch-alignment rule that pairs a rover epoch with a\n"
                    "base epoch, including the case where the base has already passed it.\n"
                    "With no input on stdin it runs a built-in example; see the header\n"
                    "comment for the input format.\n";
            return 0;
        }
        cerr << "Error: unknown option '" << a << "'\n";
        return 2;
    }

    vector<CommonTime> rover, base;
    bool fromStdin = parseInput(cin, rover, base);

    if (!fromStdin) {
        if (!isatty(0)) {
            cerr << "Note: could not read two epoch lists from stdin; "
                    "running the built-in example.\n";
        }
        istringstream in(BUILT_IN);
        rover.clear();
        base.clear();
        parseInput(in, rover, base);
    }

    align(rover, base);
    return 0;
}
