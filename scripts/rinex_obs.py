"""Minimal RINEX 3 observation reader/writer.

Enough of the format to read a satellite's phase/pseudorange series and to
rewrite individual observation fields in place, which is what the cycle-slip
injector needs. Not a general-purpose RINEX library.

Format notes that matter here (RINEX 3.04/3.05):

* The header is a sequence of 80-character records whose last 20 characters
  hold a label, terminated by an ``END OF HEADER`` record.
* ``SYS / # / OBS TYPES`` gives, per constellation, the ordered observation
  types. The order is what defines the column layout of every data record, and
  a constellation with more than 13 types continues on further
  ``SYS / # / OBS TYPES`` records that leave the system character blank.
* An epoch starts with a ``>`` record. It carries the epoch flag at column 31
  and the number of satellites at columns 32-34.
* Each satellite then occupies exactly one record. After the 3-character
  satellite id, observable *j* occupies 16 characters: a 14-character value
  followed by the LLI and SSI flags.

Carrier phase is stored in **cycles**, not metres. The C++ reader multiplies by
the wavelength when it loads a file, so adding N to a phase field here is an
N-cycle slip by the time the detector sees it.
"""

import os
import sys

# Widths fixed by the format.
LABEL_START = 60
LABEL_WIDTH = 20
VALUE_WIDTH = 14
FIELD_WIDTH = 16
SAT_ID_WIDTH = 3

EPOCH_FLAG_COL = 31
NUM_SAT_START = 32
NUM_SAT_WIDTH = 3


class RinexError(Exception):
    """Raised for anything that makes the file unreadable or non-RINEX."""


def parse_obs_types(header_lines):
    """Return {system: [obs_type, ...]} from the header's SYS / # / OBS TYPES.

    The declared count is checked against what was actually read: it is the one
    cheap way to catch a mis-parse here, and a mis-parse silently corrupts every
    column index downstream.
    """
    obs_types = {}
    declared = {}
    current = None

    for line in header_lines:
        if len(line) <= LABEL_START:
            continue
        if line[LABEL_START:LABEL_START + LABEL_WIDTH].strip() != "SYS / # / OBS TYPES":
            continue

        system = line[0:1].strip()
        if system:
            # First (and only) record for this constellation: it carries the count.
            declared[system] = int(line[3:6])
            current = system
            obs_types[current] = []
        elif current is None:
            # A continuation record with nothing to continue; malformed, and
            # guessing which constellation it belonged to would be worse.
            continue

        # Up to 13 three-character codes per record, ending at column 60.
        for i in range(13):
            start = 4 * i + 7
            if start + 3 > LABEL_START:
                break
            obs = line[start:start + 3].strip()
            if not obs:
                break
            obs_types[current].append(obs)

    for system, count in declared.items():
        if len(obs_types[system]) != count:
            raise RinexError(
                "declared %d observation types for %s but parsed %d"
                % (count, system, len(obs_types[system])))

    return obs_types


def read_header(path):
    """Return (header_lines, obs_types, data_start_line_index).

    ``header_lines`` keeps the newline characters so they can be written back
    verbatim.
    """
    header_lines = []
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as fh:
        for lineno, line in enumerate(fh):
            header_lines.append(line)
            # Tolerate records shorter than 80 characters: a file whose trailing
            # whitespace has been stripped - scripts/make_sample_data.py produces
            # exactly that, and it is the committed sample - ends its header at
            # column 72 rather than 79.
            if len(line) > LABEL_START and \
                    line[LABEL_START:LABEL_START + LABEL_WIDTH].strip() == "END OF HEADER":
                return header_lines, parse_obs_types(header_lines), lineno + 1
    raise RinexError("no END OF HEADER record found in %s" % path)


def parse_epoch_line(line):
    """Return (flag, num_satellites) from a '>'-prefixed epoch record."""
    if not line.startswith(">"):
        raise RinexError("expected an epoch record, got: %r" % line[:60])
    flag = int(line[EPOCH_FLAG_COL:EPOCH_FLAG_COL + 1].strip() or "0")
    num_sat = int(line[NUM_SAT_START:NUM_SAT_START + NUM_SAT_WIDTH].strip() or "0")
    return flag, num_sat


def parse_epoch_time(line):
    """Return (year, month, day, hour, minute, second) from an epoch record."""
    try:
        return (
            int(line[2:6]),
            int(line[7:9]),
            int(line[10:12]),
            int(line[13:15]),
            int(line[16:18]),
            float(line[19:30]),
        )
    except ValueError as exc:
        raise RinexError("malformed epoch record: %r (%s)" % (line[:60], exc))


def satellite_of(record):
    """Satellite id of a data record, e.g. 'C01'."""
    return record[:SAT_ID_WIDTH]


def field_span(obs_index):
    """(start, end) of observation *obs_index*'s 14-character value field."""
    start = SAT_ID_WIDTH + FIELD_WIDTH * obs_index
    return start, start + VALUE_WIDTH


def get_value(record, obs_index):
    """Value of observation *obs_index*, or None when the field is blank."""
    start, end = field_span(obs_index)
    if end > len(record):
        return None
    text = record[start:end].strip()
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def set_value(record, obs_index, value):
    """Return *record* with observation *obs_index* set to *value*.

    Pads the record with spaces if the field lies beyond its current end, which
    happens when the file has had trailing whitespace stripped. Fails loudly
    rather than truncating if the formatted number would not fit: a silent
    overflow here would corrupt every field after it.
    """
    start, end = field_span(obs_index)
    if end > len(record):
        record = record + " " * (end - len(record))

    text = "%14.3f" % value
    if len(text) > VALUE_WIDTH:
        raise RinexError(
            "value %r needs %d characters, the field holds %d"
            % (text, len(text), VALUE_WIDTH))

    return record[:start] + text + record[end:]


def phase_bands(obs_types):
    """Map band digit -> the phase observation codes present for it.

    RINEX phase codes are ``L<band><attribute>``. The C++ side collapses them to
    ``L<band>`` via the first two characters, and because the observation map is
    a ``std::map`` it iterates in alphabetical order - so when several codes
    share a band the alphabetically last one wins and the others are discarded.
    Callers that want a slip to actually reach the detector must modify all of
    them, which is why this returns a list.
    """
    bands = {}
    for obs in obs_types:
        if not obs.startswith("L") or len(obs) < 2:
            continue
        band = obs[1]
        if not band.isdigit():
            continue
        bands.setdefault(band, []).append(obs)
    for band in bands:
        bands[band].sort()
    return bands


def pseudorange_bands(obs_types):
    """Map band digit -> the pseudorange observation codes present for it."""
    bands = {}
    for obs in obs_types:
        if not obs.startswith("C") or len(obs) < 2:
            continue
        band = obs[1]
        if not band.isdigit():
            continue
        bands.setdefault(band, []).append(obs)
    for band in bands:
        bands[band].sort()
    return bands
