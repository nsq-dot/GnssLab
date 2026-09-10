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
#include <stdexcept>

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

    int getValueAsInt(const std::string &key);

    std::string getValueAsString(const std::string &key);

    bool getValueAsBool(const std::string &key);

    double getValueAsDouble(const std::string &key);
};

#endif // CONFIGREADER_H
