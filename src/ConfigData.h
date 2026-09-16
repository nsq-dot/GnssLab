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
#include <vector>

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
 * Cycle-slip detection configuration (chapter 7), read from the same
 * `key = value` ini format as SPPConfigData.
 *
 * Kept separate from SPPConfigData because the two programs share no keys
 * beyond the file paths and the constellation switches: the solver's elevation
 * mask, tropopause model and estimator have no meaning to a cycle-slip detector.
 * Both structs delegate to the same ConfigReader and resolvePath(), so the
 * format and the path rules stay identical.
 */
struct CSConfigData {

    // ---- files ----
    /// RINEX observation file, relative to the config file's directory.
    std::string obsFile;
    /// Directory for the per-satellite and summary output, relative to the
    /// config file's directory. Replaces the hardcoded input-directory write
    /// that used to scatter `G01`, `C08`, ... into data/.
    std::string outDir;

    // ---- epoch control ----
    /// Stop after this epoch, as "YYYY-MM-DDTHH:MM:SS". Empty means "run to the
    /// end of the observation file".
    std::string stopUTC;

    // ---- constellation selection ----
    bool GPS;
    bool BD2;

    // ---- detector parameters ----
    /// Epoch gap beyond which the arc is treated as interrupted. The textbook
    /// suggests 30 min; 120 s is stricter and suits a 1 Hz file. [s]
    double deltaTMax;
    /// Absolute detection threshold for the GF statistic. [m]
    ///
    /// NOT a multiple of a wavelength: the smallest slip the GF combination can
    /// resolve is |lambda1 - lambda2| = 0.0539 m, and a threshold placed *at*
    /// that value catches such a slip only about half the time. 0.030 m sits at
    /// 0.56x of it and keeps the false-alarm rate at zero on 1 Hz static data.
    double threshold;
    /// Sliding-window length for the polynomial-fit detector. [epochs]
    int gfPolyWindow;

    /// Values used when no config file is supplied or a key is absent.
    static CSConfigData defaults();

    /**
     * Read a configuration file. Every key is optional; anything absent keeps
     * its `defaults()` value. Throws std::runtime_error only if the file exists
     * but cannot be opened. Relative paths are returned as written - use
     * resolvePath() to make them absolute against the config file's directory.
     */
    static CSConfigData fromIni(const std::string &path);
};

/**
 * Systematic-bias / RINEX-inventory configuration, shared by
 * apps/system_bias and apps/read_rinex, read from `config/bias.ini`.
 *
 * One struct for both because they need exactly the same things - an
 * observation file, a broadcast navigation file, somewhere to write, and a stop
 * epoch - and nothing else. The rule is the one stated above CSConfigData: split
 * the struct when the programs stop sharing more than file paths. The TGD and
 * ionospheric diagnostics system_bias computes have no meaning to read_rinex's
 * inventory, and neither has any setting of its own.
 *
 * CAUTION, and the reason this comment is longer than the struct: the two
 * programs used to stop at *different* epochs - system_bias at 01:00:30,
 * read_rinex at 00:00:30. One `stopUTC` key cannot carry both, and the shipped
 * value is system_bias's. read_rinex therefore now runs 123 epochs where it used
 * to run 3; 00:00:30 was a debugging leftover (read_rinex's own commented-out
 * alternative was "23:59:30", i.e. the whole day). If you "tidy" this back to
 * 00:00:30 you will change system_bias's output instead - and its nine files are
 * the point of that program. See CHANGELOG.md.
 */
struct BiasConfigData {

    // ---- files ----
    /// RINEX observation file, relative to the config file's directory.
    std::string obsFile;
    /// RINEX navigation file (broadcast ephemeris).
    std::string navFile;
    /// Directory for the diagnostic output, relative to the config file's
    /// directory. Replaces the fixed-name files (GNSS_Statistics.csv,
    /// GPS_TGD_Result.csv, ...) that used to land in the *working* directory,
    /// which meant a run through the `gnss app` CLI dirtied the repository root.
    ///
    /// Shared by both programs; their file names do not overlap, so one
    /// directory holds both sets.
    std::string outDir;

    // ---- epoch control ----
    /// Stop after this epoch, as "YYYY-MM-DDTHH:MM:SS". Empty means "run to the
    /// end of the observation file".
    std::string stopUTC;

    /// Values used when no config file is supplied or a key is absent.
    static BiasConfigData defaults();

    /**
     * Read a configuration file. Every key is optional; anything absent keeps
     * its `defaults()` value. Throws std::runtime_error only if the file exists
     * but cannot be opened. Relative paths are returned as written - use
     * resolvePath() to make them absolute against the config file's directory.
     */
    static BiasConfigData fromIni(const std::string &path);
};

/**
 * Broadcast-versus-precise ephemeris configuration, shared by apps/bds_eph and
 * apps/bds_gps_diff, read from `config/eph.ini`.
 *
 * Separate from SPPConfigData even though both name a navigation file: the
 * solver has no use for a precise orbit, and the comparison programs have no use
 * for an elevation mask. The first four keys are shared by the two programs; the
 * rest are per-program, and each program ignores the ones that are not its own.
 *
 * `bds_eph` reports one satellite at one epoch; `bds_gps_diff` sweeps a list of
 * satellites over a time series. That asymmetry is why the two halves are
 * documented separately below rather than folded into one set of keys.
 */
struct EphConfigData {

    // ---- files ----
    /// RINEX navigation file (broadcast ephemeris).
    std::string navFile;
    /// MGEX precise orbit (SP3), the reference the broadcast orbit is compared
    /// against. Not committed - see data/README.md for where to download it.
    std::string sp3File;
    /// Output directory, relative to the config file's directory.
    ///
    /// Unused by bds_eph, which prints its report and writes no files. It
    /// deliberately has no `--out-dir` option either: accepting one as a silent
    /// no-op would be worse than not offering it.
    std::string outDir;

    // ---- bds_eph: one satellite at one epoch ----
    /// Satellite to compare, e.g. "C01".
    std::string targetSat;
    /// The epoch to evaluate, as "YYYY-MM-DDTHH:MM:SS".
    std::string targetUTC;

    // ---- bds_gps_diff: a satellite set over a time series ----
    /// Satellites to sweep.
    ///
    /// A list cannot be expressed as a single ini value, and ConfigReader has no
    /// list accessor, so the file carries this as ONE comma-separated string and
    /// fromIni() splits it. Order is preserved: it is the order of the CSV rows.
    std::vector<std::string> satList;
    /// First epoch of the series, as "YYYY-MM-DDTHH:MM:SS". Also names the output
    /// file (sat_pos_vel_diff_<yyyymmdd>.csv), so a series starting on another
    /// day no longer produces a file whose name says 20250101.
    std::string startUTC;
    /// Number of epochs to process.
    int epochCount;
    /// Seconds between epochs. The SP3 product is sampled at 5 min, so the
    /// intermediate epochs rely on the orbit interpolation.
    double interval;

    /// Values used when no config file is supplied or a key is absent.
    static EphConfigData defaults();

    /**
     * Read a configuration file. Every key is optional; anything absent keeps
     * its `defaults()` value. Throws std::runtime_error only if the file exists
     * but cannot be opened. Relative paths are returned as written - use
     * resolvePath() to make them absolute against the config file's directory.
     */
    static EphConfigData fromIni(const std::string &path);
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

/**
 * Find a config file by walking up from the working directory.
 *
 * `relative` is a config path as it is normally written from the repository
 * root, e.g. "config/spp.ini". The search starts in the current working
 * directory, climbs one directory at a time, and returns the first candidate
 * that exists as a regular file - or "" if the walk reaches the filesystem root
 * without a hit.
 *
 * This exists because the default config name is relative to the repository
 * root, while the *working directory* frequently is not: CLion runs a target
 * from its build tree (cmake-build-debug/bin here, two levels down). A miss is
 * not harmless - the program falls back to built-in defaults, whose paths are
 * relative to the working directory, so the run then dies with "cannot open
 * observation file" and reads like a broken build rather than a missing config.
 *
 * An absolute `relative` is returned unchanged: the caller has already checked
 * that one, and a path the user typed should fail loudly rather than quietly
 * resolve to a different file.
 */
std::string findConfigUpwards(const std::string &relative);

/**
 * Split a comma-separated config value into tokens.
 *
 * The reader knows only scalars, so a list has to travel as one string; this is
 * the single place that turns such a string into a vector, so the ini format,
 * the config loader and any command-line override that accepts a list all agree
 * on what "a, b ,,c" means.
 *
 * Both ends of every token are trimmed - ConfigReader trims only the left of the
 * whole value, so "a, b" arrives with a space still attached to "b" - and empty
 * tokens are dropped, which makes a trailing comma harmless and lets a caller
 * treat an empty result as "no list given".
 */
std::vector<std::string> splitList(const std::string &value);
