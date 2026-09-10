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

#include "ConfigReader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>

bool ConfigReader::isComment(const std::string &line) {
    return line.find("#") == 0 || line.empty();
}

bool ConfigReader::isEmpty(const std::string &line) {
    return line.find_first_not_of(" \t\n\r\f\v") == std::string::npos;
}

ConfigReader::ConfigReader(const std::string &filename) {
    std::ifstream file(filename);
    std::string line;

    if (file.is_open()) {
        while (getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\n\r\f\v")); // Trim leading whitespace
            if (!isComment(line) && !isEmpty(line)) {
                size_t delimiterPos = line.find("=");
                if (delimiterPos != std::string::npos) {
                    std::string key = line.substr(0, delimiterPos);
                    std::string value = line.substr(delimiterPos + 1);
                    // Trim whitespace from key and value
                    key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
                    value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
                    config[key] = value;
                }
            }
        }
        file.close();
    } else {
        throw std::runtime_error("Unable to open file: " + filename);
    }
}

int ConfigReader::stringToInt(const std::string &str) {
    try {
        return std::stoi(str);
    } catch (const std::exception &e) {
        throw std::runtime_error("Invalid integer value: " + str);
    }
}

double ConfigReader::stringToDouble(const std::string &str) {
    try {
        return std::stod(str);
    } catch (const std::exception &e) {
        throw std::runtime_error("Invalid double value: " + str);
    }
}

std::string ConfigReader::getConfigValue(const std::string &key) {
    auto it = config.find(key);
    if (it == config.end()) {
        throw std::runtime_error("Key not found: " + key);
    }
    return it->second;
}

int ConfigReader::getValueAsInt(const std::string &key) {
    return stringToInt(getConfigValue(key));
}

std::string ConfigReader::getValueAsString(const std::string &key) {
    return getConfigValue(key);
}

namespace {

// Classify a boolean spelling. Accepts 1/0, true/false, yes/no, on/off,
// case-insensitively. Returns TRUE/FALSE/UNRECOGNISED so callers can decide
// whether an unknown spelling is an error (getValueAsBool) or a fallback
// (getValueAsBoolOr).
enum BoolSpelling {
    BOOL_TRUE = 1,
    BOOL_FALSE = 0,
    BOOL_UNRECOGNISED = -1
};

BoolSpelling classifyBool(const std::string &str) {
    std::string v;
    v.reserve(str.size());
    for (char c: str) v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (v == "1" || v == "true" || v == "yes" || v == "on") return BOOL_TRUE;
    if (v == "0" || v == "false" || v == "no" || v == "off") return BOOL_FALSE;
    return BOOL_UNRECOGNISED;
}

}  // namespace

bool ConfigReader::getValueAsBool(const std::string &key) {
    std::string value = getConfigValue(key);

    // Previously:
    //     return value == "1" || value == "true" || stringToInt(value) != 0;
    // The `||` short-circuit means the last term runs only for values that are
    // neither "1" nor "true", so `est = yes` fell into stringToInt() and threw
    // "Invalid integer value: yes" rather than being interpreted.
    BoolSpelling b = classifyBool(value);
    if (b == BOOL_UNRECOGNISED) {
        throw std::runtime_error("Invalid boolean value for key '" + key + "': " + value);
    }
    return b == BOOL_TRUE;
}

double ConfigReader::getValueAsDouble(const std::string &key) {
    return stringToDouble(getConfigValue(key));
}

// --- non-throwing variants -------------------------------------------------
// These exist so a partial config file is usable: every key is optional and
// anything absent keeps the caller's default.

int ConfigReader::getValueAsIntOr(const std::string &key, int defaultValue) {
    auto it = config.find(key);
    if (it == config.end()) return defaultValue;
    try {
        return stringToInt(it->second);
    } catch (const std::runtime_error &) {
        return defaultValue;
    }
}

double ConfigReader::getValueAsDoubleOr(const std::string &key, double defaultValue) {
    auto it = config.find(key);
    if (it == config.end()) return defaultValue;
    try {
        return stringToDouble(it->second);
    } catch (const std::runtime_error &) {
        return defaultValue;
    }
}

bool ConfigReader::getValueAsBoolOr(const std::string &key, bool defaultValue) {
    auto it = config.find(key);
    if (it == config.end()) return defaultValue;
    BoolSpelling b = classifyBool(it->second);
    if (b == BOOL_UNRECOGNISED) return defaultValue;
    return b == BOOL_TRUE;
}

std::string ConfigReader::getValueAsStringOr(const std::string &key, const std::string &defaultValue) {
    auto it = config.find(key);
    return (it == config.end()) ? defaultValue : it->second;
}

bool ConfigReader::has(const std::string &key) const {
    return config.find(key) != config.end();
}

std::vector<std::string> ConfigReader::keys() const {
    std::vector<std::string> result;
    result.reserve(config.size());
    for (const auto &kv: config) result.push_back(kv.first);
    return result;
}