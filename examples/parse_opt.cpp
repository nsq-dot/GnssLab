/**
 * Copyright:
 *  This software is licensed under the Mulan Permissive Software License, Version 2 (MulanPSL-2.0).
 *  You may obtain a copy of the License at:http://license.coscl.org.cn/MulanPSL2
 *  As stipulated by the MulanPSL-2.0, you are granted the following freedoms:
 *      To copy, use, and modify the software;
 *      To use the software for commercial purposes;
 *      To redistribute the software.
 *
 * Author: shoujian zhang，shjzhang@sgg.whu.edu.cn， 2024-10-10
 *
 * References:
 * 1. Sanz Subirana, J., Juan Zornoza, J. M., & Hernández-Pajares, M. (2013).
 *    GNSS data processing: Volume I: Fundamentals and algorithms. ESA Communications.
 * 2. Eckel, Bruce. Thinking in C++. 2nd ed., Prentice Hall, 2000.
 */


#include <iostream>
#include <cstdlib> // 用于 std::exit
#include <stdexcept> // 用于 std::invalid_argument
#include <cstring> // Required for strcmp

using namespace std;
// 函数声明
double add(double a, double b);

double subtract(double a, double b);

double multiply(double a, double b);

double divide(double a, double b);

int main(int argc, char *argv[]) {

    string helpStr
    = "Usage: "
      "  calculator <num1> <operation> <num2>\n"
      "  Operations only support: +, -, *, /\n"
      "warning:\n"
      "  whitespace must be given between num1 operation and num2!\n"
      "examples:\n"
      "  calculator 2 * 4 \n"
      "  calculator 2 / 4 \n"
      "author:"
      "     shjzhang, shjzhang@sgg.whu.edu.cn";


    if (argc == 2)
    {
        cout << argv[0] << endl;
        cout << argv[1] << endl;
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            cout << helpStr << endl;
        }
    }

    // 检查参数数量
    if (argc != 4) {
        std::cerr << "Usage: calculator <num1> <operation> <num2>\n";
        std::cerr << "Operations: +, -, *, /\n";
        return 1;
    }

    // 解析输入参数
    double num1 = std::atof(argv[1]);
    std::string operation = argv[2];
    double num2 = std::atof(argv[3]);
    double result;

    try {
        if (operation == "+") {
            result = add(num1, num2);
        } else if (operation == "-") {
            result = subtract(num1, num2);
        } else if (operation == "*") {
            result = multiply(num1, num2);
        } else if (operation == "/") {
            if (num2 == 0) {
                throw std::invalid_argument("Division by zero is not allowed.");
            }
            result = divide(num1, num2);
        } else {
            std::cerr << "Invalid operation: " << operation << "\n";
            return 1;
        }

        std::cout << "Result: " << result << "\n";
    } catch (const std::invalid_argument &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// 函数定义
double add(double a, double b) {
    return a + b;
}

double subtract(double a, double b) {
    return a - b;
}

double multiply(double a, double b) {
    return a * b;
}

double divide(double a, double b) {
    return a / b;
}