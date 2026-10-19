#ifndef SINEXBIASSTORE_HPP
#define SINEXBIASSTORE_HPP

#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Exception.h"
#include "GnssStruct.h"
#include "TimeStruct.h"

/**
 * A Bias-SINEX (SINEX-BIAS, IGS v1.00) reader for observable-specific biases.
 *
 * Only satellite OSB records are kept, and only code observables: the position
 * solvers in this repository need a per-satellite, per-signal correction to the
 * pseudorange, and nothing else. DSB records, station-specific records and
 * phase records are counted and skipped (see the counters below). The values
 * are converted to metres at load time, so a caller never sees a unit.
 *
 * The sign convention is the one the format defines:
 *
 *     bias = observed - true        =>        true = observed - bias
 *
 * A caller therefore **subtracts** the value getOsb() returns from the code
 * observation. (SPPIFCode::correctCodeBias is the caller that does so.)
 *
 * Why this exists: a precise satellite clock is referred to a particular
 * ionosphere-free observation combination. GFZ's and WHU's BeiDou clocks are
 * both referred to C2I/C6I (B1I/B3I), while the RTK program observes C2I/C7I
 * (BDS-2) and C2I/C5P (BDS-3). The per-satellite difference between those
 * combinations is a code bias of up to tens of metres; without it the BDS
 * columns get worse, not better, when precise products are switched on.
 *
 * Validity intervals are half-open, [start, end). The epoch is stored as the
 * file's own time system (nearly always GPS); getOsb() converts a query epoch
 * to it when they differ.
 */
class SinexBiasStore {
public:

    struct OsbInterval {
        CommonTime start;
        CommonTime end;           // exclusive
        double valueMetres;
    };

    SinexBiasStore() { clear(); }

    /**
     * Read a Bias-SINEX file.
     *
     * @throws FileMissingException if the file cannot be opened
     * @throws FFStreamError if the version is not 1.00, the bias mode is not
     *         ABSOLUTE, the time system is not recognised, a record is
     *         malformed, or the file carries no usable code OSB records.
     */
    void loadFile(const std::string &filename) noexcept(false);

    /**
     * The OSB for one satellite and one raw three-character RINEX observable,
     * in metres, at an epoch. Returns false when the satellite, the observable
     * or a validity interval covering @p time is absent.
     */
    bool getOsb(const SatID &sat,
                const std::string &obsType3,
                const CommonTime &time,
                double &metres) const;

    /// All validity intervals stored for one (satellite, observable), or null.
    const std::vector<OsbInterval> *getIntervals(
            const SatID &sat, const std::string &obsType3) const;

    /// Whether the file carries any code OSB for a system+observable, e.g. C/C5P.
    bool hasObsType(const std::string &system,
                    const std::string &obsType3) const;

    bool empty() const { return osbData.empty(); }

    // Metadata, for the inspector's summary and the RTK manifest.
    const std::string &getVersion() const { return version; }
    const std::string &getBiasMode() const { return biasMode; }
    const std::string &getTimeSystemCode() const { return timeSystemCode; }
    const TimeSystem &getTimeSystem() const { return biasTimeSystem; }
    const std::map<std::string, std::vector<std::string>> &
            getClockReferences() const { return clockReferences; }

    std::size_t getHeaderRecordCount() const { return headerRecordCount; }
    std::size_t getTotalBiasRecords() const { return totalBiasRecords; }
    std::size_t getCodeIntervalCount() const { return codeIntervalCount; }
    std::size_t getPhaseRecordCount() const { return phaseRecordCount; }
    std::size_t getDsbRecordCount() const { return dsbRecordCount; }
    std::size_t getStationRecordCount() const { return stationRecordCount; }
    std::size_t getSatelliteCount() const { return osbData.size(); }

    /// Code observables present, keyed by constellation: {"C": {"C2I", ...}}.
    const std::map<std::string, std::set<std::string>> &
            getCodeObsTypes() const { return codeObsTypes; }

private:

    void clear();

    void readHeader(std::fstream &strm);
    void readData(std::fstream &strm);

    void parseDescriptionLine(const std::string &line);
    void parseRecord(const std::string &line, std::size_t lineNo);
    void parseRecordFixedWidth(const std::string &line, std::size_t lineNo,
                               bool &ok);
    void parseRecordTokens(const std::string &line, std::size_t lineNo);

    void storeRecord(const std::string &type,
                     const std::string &prn,
                     const std::string &station,
                     const std::string &obs1,
                     const std::string &obs2,
                     const std::string &startTok,
                     const std::string &endTok,
                     const std::string &unit,
                     const std::string &valueTok,
                     std::size_t lineNo);

    CommonTime parseEpoch(const std::string &token,
                          std::size_t lineNo) const;

    double parseValueMetres(const std::string &unit,
                            const std::string &valueTok,
                            std::size_t lineNo) const;

    void addInterval(const SatID &sat, const std::string &obsType3,
                     const CommonTime &start, const CommonTime &end,
                     double metres);

    std::map<SatID, std::map<std::string, std::vector<OsbInterval>>> osbData;

    // Code observables per constellation, for the coverage check in apps/rtk.
    std::map<std::string, std::set<std::string>> codeObsTypes;

    std::string version;
    std::string biasMode;
    std::string timeSystemCode;
    TimeSystem biasTimeSystem;
    std::map<std::string, std::vector<std::string>> clockReferences;

    std::size_t headerRecordCount = 0;  // the count declared in %=BIA
    std::size_t totalBiasRecords = 0;   // OSB + DSB lines seen
    std::size_t codeIntervalCount = 0;  // code OSB intervals stored
    std::size_t phaseRecordCount = 0;
    std::size_t dsbRecordCount = 0;
    std::size_t stationRecordCount = 0;
    std::string filenameForErrors;
};

#endif // SINEXBIASSTORE_HPP
