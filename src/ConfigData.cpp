/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Implementation of the solver configuration loader. See ConfigData.h.
 */

#include "ConfigData.h"
#include "ConfigReader.h"

#include <algorithm>
#include <cctype>

namespace {

// Normalise separators to '/' so that path handling is identical on Windows and
// POSIX, and so that the result is directly usable by std::ifstream on both.
std::string toForwardSlashes(const std::string &p) {
    std::string out = p;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

bool isAbsolute(const std::string &p) {
    if (p.empty()) return false;
    // POSIX: /foo
    if (p[0] == '/') return true;
    // Windows drive-letter: C:/foo or C:foo
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') return true;
    // Windows UNC: //host/share
    if (p.size() >= 2 && p[0] == '/' && p[1] == '/') return true;
    return false;
}

std::string dirName(const std::string &path) {
    std::string p = toForwardSlashes(path);
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return p.substr(0, pos);
}

}  // namespace

std::string resolvePath(const std::string &baseDir, const std::string &path) {
    if (path.empty()) return path;
    if (isAbsolute(path)) return toForwardSlashes(path);
    if (baseDir.empty()) return toForwardSlashes(path);
    return toForwardSlashes(baseDir) + "/" + toForwardSlashes(path);
}

SPPConfigData SPPConfigData::defaults() {
    SPPConfigData c;

    c.obsFile = "data/WUH200CHN_R_20250010000_01D_30S_MO.rnx";
    c.navFile = "data/BRDC00IGS_R_20250010000_01D_MN.rnx";
    c.outDir = "output";
    c.stopUTC = "2025-01-01T00:30:30";

    c.GPS = true;
    c.BD2 = true;
    c.BD3 = true;
    c.Galileo = false;
    c.GLONASS = false;

    // 10 degrees, matching SPPIFCode's constructor default. Do NOT change this to
    // 15: that was the value in the never-wired-up placeholder ini, and adopting
    // it here would silently change the solution and break the regression test
    // in tests/baseline/.
    c.cutOffElevation = 10;
    c.minSatNum = 4;
    c.maxGDOP = 10;
    c.tropModel = 1;
    c.ionoModel = 1;
    c.obsModel = 1;
    c.enableBDSTGD = true;

    // Deliberately unused by the current solver, which carries a single scalar
    // sigma. Kept so the keys round-trip; see config/README.md.
    c.noiseGPSCode = 1.0;
    c.noiseBD2Code = 1.0;
    c.noiseBD3Code = 1.0;

    c.estimator = 1;
    return c;
}

SPPConfigData SPPConfigData::fromIni(const std::string &path) {
    SPPConfigData c = defaults();

    ConfigReader reader(path);

    // Every key is optional; a missing key keeps the default from defaults().
    // Note the *Or() accessors, which also absorb malformed values rather than
    // aborting a run over a typo in an optional setting.
    c.obsFile = reader.getValueAsStringOr("obsFile", c.obsFile);
    c.navFile = reader.getValueAsStringOr("navFile", c.navFile);
    c.stopUTC = reader.getValueAsStringOr("stopUTC", c.stopUTC);

    // `outDir` is the current spelling. `outFile` is accepted for compatibility
    // with config files written against the older placeholder ini, where it
    // named a single output file; only its directory is meaningful now.
    if (reader.has("outDir")) {
        c.outDir = reader.getValueAsStringOr("outDir", c.outDir);
    } else if (reader.has("outFile")) {
        c.outDir = dirName(reader.getValueAsStringOr("outFile", "output/x"));
    }

    c.GPS = reader.getValueAsBoolOr("GPS", c.GPS);
    c.BD2 = reader.getValueAsBoolOr("BD2", c.BD2);
    c.BD3 = reader.getValueAsBoolOr("BD3", c.BD3);
    c.Galileo = reader.getValueAsBoolOr("Galileo", c.Galileo);
    c.GLONASS = reader.getValueAsBoolOr("GLONASS", c.GLONASS);

    c.cutOffElevation = reader.getValueAsIntOr("cutOffElevation", c.cutOffElevation);
    c.minSatNum = reader.getValueAsIntOr("minSatNum", c.minSatNum);
    c.maxGDOP = reader.getValueAsIntOr("maxGDOP", c.maxGDOP);
    c.tropModel = reader.getValueAsIntOr("tropModel", c.tropModel);
    c.ionoModel = reader.getValueAsIntOr("ionoModel", c.ionoModel);
    c.obsModel = reader.getValueAsIntOr("obsModel", c.obsModel);

    c.enableBDSTGD = reader.getValueAsBoolOr("enableBDSTGD", c.enableBDSTGD);

    c.noiseGPSCode = reader.getValueAsDoubleOr("noiseGPSCode", c.noiseGPSCode);
    c.noiseBD2Code = reader.getValueAsDoubleOr("noiseBD2Code", c.noiseBD2Code);
    c.noiseBD3Code = reader.getValueAsDoubleOr("noiseBD3Code", c.noiseBD3Code);

    c.estimator = reader.getValueAsIntOr("estimator", c.estimator);
    return c;
}

CSConfigData CSConfigData::defaults() {
    CSConfigData c;

    // The committed sample: a fresh clone has this file and nothing else, so it
    // is the only honest default. The 1 Hz zero-baseline set under
    // data/Zero-baseline/ is not committed (see .gitignore) and is meant to be
    // selected with --obs or a local config.
    c.obsFile = "data/sample/WUH200CHN_R_20250010000_01D_30S_MO.rnx";
    c.outDir = "output/cs";
    c.stopUTC = "";   // run to the end of the file

    c.GPS = true;
    c.BD2 = true;

    c.deltaTMax = 120.0;
    c.threshold = 0.030;
    c.gfPolyWindow = 30;

    return c;
}

CSConfigData CSConfigData::fromIni(const std::string &path) {
    CSConfigData c = defaults();

    ConfigReader reader(path);

    // Every key is optional; a missing key keeps the default from defaults().
    c.obsFile = reader.getValueAsStringOr("obsFile", c.obsFile);
    c.stopUTC = reader.getValueAsStringOr("stopUTC", c.stopUTC);

    if (reader.has("outDir")) {
        c.outDir = reader.getValueAsStringOr("outDir", c.outDir);
    } else if (reader.has("outFile")) {
        c.outDir = dirName(reader.getValueAsStringOr("outFile", "output/cs/x"));
    }

    c.GPS = reader.getValueAsBoolOr("GPS", c.GPS);
    c.BD2 = reader.getValueAsBoolOr("BD2", c.BD2);

    c.deltaTMax = reader.getValueAsDoubleOr("deltaTMax", c.deltaTMax);
    c.threshold = reader.getValueAsDoubleOr("threshold", c.threshold);
    c.gfPolyWindow = reader.getValueAsIntOr("gfPolyWindow", c.gfPolyWindow);

    return c;
}
