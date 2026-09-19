/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Epoch synchronisation between two receivers, as a pure decision function.
 *
 * Relative positioning needs both receivers' observations for the SAME instant.
 * The two files are not guaranteed to line up: a receiver may drop an epoch, a
 * file may start or end at a different time, and after a gap one stream is
 * simply ahead of the other. The reader's job is to walk the stream forward
 * until it matches the reference, and to say so when the stream has already
 * passed the reference by more than the tolerance - in which case the reference
 * epoch has no counterpart and must be skipped.
 *
 * This header exists so that decision can be exercised on its own.
 * RinexObsReader::parseRinexObs(CommonTime&) implements the same state machine
 * inline, in terms of stream positions and rewinds; that copy is deliberately
 * left alone (it is the code the frozen baselines were produced through), so
 * this is a second expression of the same rule rather than a replacement. The
 * two are kept in step by the behaviour, not by sharing a symbol: a change to
 * the tolerance in one without the other is a bug.
 */

#ifndef GNSSLAB_EPOCHALIGN_H
#define GNSSLAB_EPOCHALIGN_H

#include "TimeStruct.h"

/// What to do with a stream epoch that has just been read against a reference.
enum class EpochAlign {
    /// Same instant to within the tolerance - use it, and read the next one
    /// from the stream for the following reference.
    MATCH,
    /// The stream is behind the reference. Read the next epoch from the stream
    /// and ask again.
    STREAM_BEHIND,
    /// The stream has gone past the reference by more than the tolerance, so it
    /// skipped the reference epoch. That reference has no counterpart: rewind
    /// the stream to the epoch just read and move on to the next reference.
    STREAM_AHEAD
};

/**
 * Decide what to do with `streamEpoch` given the `refEpoch` we want.
 *
 * `tolerance` is in seconds. The reader uses 0.001 s, which absorbs the
 * millisecond rounding in a RINEX epoch line while still catching a receiver
 * that is genuinely on a different second.
 *
 * The comparison is asymmetric on purpose, and mirrors the reader: an epoch is
 * accepted when it is at or after the reference and no more than `tolerance`
 * beyond it. An epoch strictly before the reference is not an error - the
 * stream simply has not caught up yet.
 */
inline EpochAlign alignEpochs(const CommonTime &streamEpoch,
                              const CommonTime &refEpoch,
                              double tolerance) {
    if (streamEpoch < refEpoch) {
        return EpochAlign::STREAM_BEHIND;
    }
    if (streamEpoch > refEpoch + tolerance) {
        return EpochAlign::STREAM_AHEAD;
    }
    return EpochAlign::MATCH;
}

/// The tolerance the reader uses, named so callers do not invent their own.
inline double epochSyncTolerance() {
    return 0.001;
}

#endif //GNSSLAB_EPOCHALIGN_H
