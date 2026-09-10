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

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
#include <string>
#include <exception>

// 基类异常，包含共用成员和方法
class BaseException : public std::exception {
public:
    explicit BaseException(std::string msg) : message(std::move(msg)) {}

    virtual ~BaseException() noexcept {}

    const char *what() const noexcept override {
        return message.c_str();
    }

protected:
    std::string message;
};

// 继承自基类异常的特定异常
class StringException : public BaseException {
public:
    explicit StringException(std::string msg) : BaseException(std::move(msg)) {}
};

class InvalidRequest : public BaseException {
public:
    explicit InvalidRequest(std::string msg) : BaseException(std::move(msg)) {}
};

class FFStreamError : public BaseException {
public:
    explicit FFStreamError(const std::string &msg) : BaseException(msg) {}
};

class EndOfFile : public BaseException {
public:
    explicit EndOfFile(const std::string &msg) : BaseException(msg) {}
};

class FileMissingException : public BaseException {
public:
    explicit FileMissingException(const std::string &msg) : BaseException(msg) {}
};

class ConfigException : public BaseException {
public:
    explicit ConfigException(std::string msg) : BaseException(std::move(msg)) {}
};

class GeometryException : public BaseException {
public:
    explicit GeometryException(std::string msg) : BaseException(std::move(msg)) {}
};

class TypeIDNotFound : public BaseException {
public:
    explicit TypeIDNotFound(const std::string &msg) : BaseException(msg) {}
};

class SatIDNotFound : public BaseException {
public:
    explicit SatIDNotFound(const std::string &msg) : BaseException(msg) {}
};

class NumberOfSatsMismatch : public BaseException {
public:
    explicit NumberOfSatsMismatch(const std::string &msg) : BaseException(msg) {}
};

class NumberOfTypesMismatch : public BaseException {
public:
    explicit NumberOfTypesMismatch(const std::string &msg) : BaseException(msg) {}
};

class SVNumException : public BaseException {
public:
    explicit SVNumException(const std::string &msg) : BaseException(msg) {}
};

class InvalidSolver : public BaseException {
public:
    explicit InvalidSolver(const std::string &msg) : BaseException(msg) {}
};

class SyncException : public BaseException {
public:
    explicit SyncException(const std::string &msg) : BaseException(msg) {}
};