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

#ifndef GNSSLAB_STRINGUTILS_H
#define GNSSLAB_STRINGUTILS_H

#include <iostream>
#include <string>
#include <stdexcept> // For std::invalid_argument and std::out_of_range
#include <limits>    // For std::numeric_limits<double>::max()

using namespace std;

inline string strip(std::string s) {
    // 如果字符串为空，则无需处理
    if (s.empty()) return s;

    // 移除头部的空白字符
    s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));

    // 移除尾部的空白字符
    s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);

    return s;
}

inline std::string& stripTrailing(std::string& line) {
    size_t end = line.find_last_not_of(" \t\n\r\v\f"); // 查找最后一个非空白字符的位置
    if (end != std::string::npos) {
        line.resize(end + 1); // 修改字符串的长度，去掉后面的空白字符
    } else {
        line.clear(); // 如果全是空白字符，清空字符串
    }
    return line; // 返回修改后的字符串的引用
}

inline double safeStod(const std::string &str, double defaultValue = 0.0) {
    if (str.empty() || str.find_first_not_of(' ') == std::string::npos) {
        // 字符串为空或只包含空白字符
        return defaultValue;
    }

    try {
        return std::stod(str);
    } catch (const std::invalid_argument &e) {
        // 字符串不是有效的浮点数表示
        return defaultValue;
    } catch (const std::out_of_range &e) {
        // 转换后的数值超出了 double 类型可表示的范围
        return std::numeric_limits<double>::max(); // 或者最小值，根据需求决定
    }
}

inline int safeStoi(const std::string& str) {
    if (str.empty()) {
        return 0; // 如果字符串为空，直接返回 0
    }

    size_t start = 0;
    while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++; // 跳过开头的空白字符
    }

    if (start == str.size()) {
        return 0; // 如果全是空白字符，返回 0
    }

    size_t end = str.size() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(str[end]))) {
        end--; // 跳过结尾的空白字符
    }

    std::string trimmedStr = str.substr(start, end - start + 1);

    try {
        return std::stoi(trimmedStr);
    } catch (const std::invalid_argument& e) {
        return 0; // 如果转换失败，返回 0
    } catch (const std::out_of_range& e) {
        return 0; // 如果整数超出范围，返回 0
    }
}

#endif //GNSSLAB_STRINGUTILS_H
