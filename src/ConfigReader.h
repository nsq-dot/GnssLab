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
#ifndef CONFIGREADER_H
#define CONFIGREADER_H

#include <map>
#include <string>
#include <vector>
#include <stdexcept>

/**
 * Reads a `key = value` configuration file.
 *
 * Format notes, both of which have bitten people:
 *
 *  - A line is a comment only when `#` is its *first* non-blank character. An
 *    inline trailing comment (`key = 1  # note`) is not stripped, so the value
 *    becomes "1  # note" and the typed accessors will reject it. Put comments on
 *    their own line.
 *  - Keys are case-sensitive and surrounding whitespace is trimmed.
 *
 * The `getValueAsX()` accessors throw std::runtime_error when a key is missing
 * or malformed. The `getValueAsXOr()` variants return a caller-supplied default
 * instead, which is what you usually want for an optional setting.
 */
class ConfigReader {
private:
    std::map<std::string, std::string> config;

    bool isComment(const std::string &line);

    bool isEmpty(const std::string &line);

    int stringToInt(const std::string &str);

    double stringToDouble(const std::string &str);

    std::string getConfigValue(const std::string &key);

public:
    ConfigReader(const std::string &filename);

    // --- throwing accessors: the key must be present and well-formed ---

    int getValueAsInt(const std::string &key);

    std::string getValueAsString(const std::string &key);

    bool getValueAsBool(const std::string &key);

    double getValueAsDouble(const std::string &key);

    // --- non-throwing accessors: absent or malformed yields the default ---

    int getValueAsIntOr(const std::string &key, int defaultValue);

    double getValueAsDoubleOr(const std::string &key, double defaultValue);

    bool getValueAsBoolOr(const std::string &key, bool defaultValue);

    std::string getValueAsStringOr(const std::string &key, const std::string &defaultValue);

    // --- introspection ---

    /// True if the key is present, regardless of whether its value parses.
    bool has(const std::string &key) const;

    /// Every key found in the file, in sorted order.
    std::vector<std::string> keys() const;
};

#endif // CONFIGREADER_H
