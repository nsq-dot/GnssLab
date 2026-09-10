/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *
 * Course example 1.2 — reading a configuration file.
 *
 * Reads a `key = value` configuration file and prints every entry it finds,
 * then demonstrates the typed accessors on a few keys.
 *
 * This example also documents two behaviours worth knowing about before you
 * write your own configuration file:
 *
 *   - Comments are only recognised when `#` is the *first* non-blank character
 *     of a line. An inline trailing comment (`key = 1  # note`) is not stripped
 *     and becomes part of the value.
 *   - The typed accessors throw std::runtime_error if the key is absent or the
 *     value does not parse. Use the `...Or` variants when a key is optional.
 *
 * Usage: parse_config [path/to/file.ini]      (default: config/spp.ini)
 */

#include "ConfigReader.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <stdexcept>

using namespace std;

// The keys this example knows how to interpret with a type. Anything else in
// the file is still printed, just as a raw string.
struct TypedKey {
    const char *key;
    char type;  // 'i' int, 'd' double, 'b' bool, 's' string
};

static const vector<TypedKey> TYPED_KEYS = {
        {"cutOffElevation", 'i'},
        {"minSatNum",       'i'},
        {"maxGDOP",         'i'},
        {"tropModel",       'i'},
        {"ionoModel",       'i'},
        {"obsModel",        'i'},
        {"estimator",       'i'},
        {"noiseGPSCode",    'd'},
        {"noiseBD2Code",    'd'},
        {"noiseBD3Code",    'd'},
        {"GPS",             'b'},
        {"BD2",             'b'},
        {"BD3",             'b'},
        {"Galileo",         'b'},
        {"GLONASS",         'b'},
        {"obsFile",         's'},
        {"navFile",         's'},
        {"outFile",         's'},
        {"outDir",          's'},
};

static char lookupType(const string &key) {
    for (const auto &t: TYPED_KEYS) {
        if (key == t.key) return t.type;
    }
    return '?';
}

int main(int argc, char *argv[]) {
    string configFile = (argc > 1) ? argv[1] : "config/spp.ini";

    cout << "==========================================================\n";
    cout << "              Configuration file reader\n";
    cout << "==========================================================\n";
    cout << "File: " << configFile << "\n\n";

    ConfigReader reader(configFile);

    // The reader has no "list all keys" accessor, so we exercise it against the
    // set of keys this example understands. Absent keys are reported, not fatal.
    cout << left << setw(20) << "KEY" << setw(8) << "TYPE" << "VALUE\n";
    cout << string(60, '-') << "\n";

    for (const auto &t: TYPED_KEYS) {
        cout << left << setw(20) << t.key;

        try {
            switch (t.type) {
                case 'i':
                    cout << setw(8) << "int" << reader.getValueAsInt(t.key);
                    break;
                case 'd':
                    cout << setw(8) << "double" << reader.getValueAsDouble(t.key);
                    break;
                case 'b':
                    cout << setw(8) << "bool" << (reader.getValueAsBool(t.key) ? "true" : "false");
                    break;
                case 's':
                default:
                    cout << setw(8) << "string" << reader.getValueAsString(t.key);
                    break;
            }
        } catch (const std::runtime_error &e) {
            // Key missing or malformed - report it and carry on, so one bad
            // entry does not hide the rest of the file.
            cout << setw(8) << "?" << "(not present)";
        }
        cout << "\n";
    }

    cout << "\nDone.\n";
    return 0;
}
