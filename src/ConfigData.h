/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Author: Shoujian Zhang，shjzhang@sgg.whu.edu.cn， 2024-10-10
 *
 * References:
 * 1. Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
 *    GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
 * 2. Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
 */
#pragma once

#include <string>

/**
 * Solver configuration, as read from a `key = value` ini file.
 *
 * This is a plain value type. An earlier revision declared a file-scope
 * `extern SPPConfigData sppConfigData;` that was never defined anywhere in the
 * project, so any translation unit that touched it would have failed to link.
 * Pass a ConfigData around explicitly instead; a mutable global would also make
 * the solver untestable.
 *
 * Keys not listed here have no consumer in the current code. See
 * config/README.md for the full key reference, including which keys are
 * reserved for future use.
 */
struct SPPConfigData {

    // ---- files ----
    /// RINEX observation file, relative to the config file's directory.
    std::string obsFile;
    /// RINEX navigation file (broadcast ephemeris).
    std::string navFile;
    /// Directory for the solver output, relative to the config file's directory.
    /// Replaces the old single `outFile` key: the SPP program writes several
    /// related files and naming them relative to one directory keeps a run self-
    /// contained.
    std::string outDir;

    // ---- epoch control ----
    /// Stop after this epoch, as "YYYY-MM-DDTHH:MM:SS". Empty means "run to the
    /// end of the observation file".
    std::string stopUTC;

    // ---- constellation selection ----
    bool GPS;
    bool BD2;
    bool BD3;
    bool Galileo;
    bool GLONASS;

    // ---- solver options ----
    int cutOffElevation;   ///< elevation mask, degrees
    int minSatNum;
    int maxGDOP;
    int tropModel;         ///< 1 Hopfield, 2 Saastamoinen, 3 Neill
    int ionoModel;         ///< 1 Klobuchar, 2 GIM
    int obsModel;          ///< 1 IF code combination (the only one implemented)
    bool enableBDSTGD;     ///< apply the BeiDou broadcast TGD correction

    double noiseGPSCode;
    double noiseBD2Code;
    double noiseBD3Code;

    int estimator;         ///< 1 least squares, 2 Kalman (not yet wired)

    /**
     * The values used when no config file is supplied, or when a key is absent.
     *
     * These reproduce the behaviour of the hardcoded constants that preceded the
     * config system, so a run with no config file gives the same answer as the
     * original program. In particular `cutOffElevation` is 10, matching
     * SPPIFCode's own default - not the 15 that the old placeholder ini carried.
     */
    static SPPConfigData defaults();

    /**
     * Read a configuration file.
     *
     * Every key is optional: anything absent keeps its `defaults()` value, so a
     * three-line ini is valid. Throws std::runtime_error only if the file exists
     * but cannot be opened.
     *
     * Relative paths are *not* resolved here - they are returned as written. Use
     * resolvePath() to make them absolute against the config file's directory.
     */
    static SPPConfigData fromIni(const std::string &path);
};

/**
 * Resolve `path` against `baseDir` unless it is already absolute.
 *
 * Config files are meant to be readable from any working directory, so their
 * relative paths have to be interpreted against the config file's own location
 * rather than the process CWD. Handles both '/' and '\\' separators and both
 * drive-letter (`D:\\...`) and UNC (`\\\\host\\...`) absolute forms, since this
 * has to behave the same on Windows and on Linux/CI.
 */
std::string resolvePath(const std::string &baseDir, const std::string &path);
