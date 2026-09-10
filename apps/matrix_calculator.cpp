#include <iostream>
#include <cstdlib>
#include <stdexcept>
#include <cstring>
#include <string>
#include <Eigen/Dense>

using namespace std;
using namespace Eigen;

// 函数声明（矩阵运算）
MatrixXd matrix_add(const MatrixXd& a, const MatrixXd& b);
MatrixXd matrix_subtract(const MatrixXd& a, const MatrixXd& b);
MatrixXd matrix_multiply(const MatrixXd& a, const MatrixXd& b);
MatrixXd matrix_inverse(const MatrixXd& a);
MatrixXd matrix_transpose(const MatrixXd& a);

// 辅助函数：从命令行字符串解析矩阵（格式：行,列:元素1,元素2,...）
MatrixXd parse_matrix(const string& mat_str);

int main(int argc, char *argv[]) {
    // 帮助文档
    string helpStr =
        "Usage: \n"
        "  matrix_calc <matrix1> <operation> <matrix2>    # 加减乘\n"
        "  matrix_calc <operation> <matrix1>              # 求逆/转置\n"
        "Operations support: +, -, *, inv(求逆), tran(转置)\n"
        "Matrix format: rows,cols:val1,val2,...,valN\n"
        "warning:\n"
        "  whitespace must be given between parameters!\n"
        "examples:\n"
        "  matrix_calc 2,2:1,2,3,4 + 2,2:5,6,7,8    # 2x2矩阵加法\n"
        "  matrix_calc 2,2:1,2,3,4 * 2,2:5,6,7,8    # 矩阵乘法\n"
        "  matrix_calc inv 2,2:1,2,3,4              # 矩阵求逆\n"
        "  matrix_calc tran 2,2:1,2,3,4             # 矩阵转置\n";

    // 帮助参数处理
    if (argc == 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            cout << helpStr << endl;
            return 0;
        }
    }

    try {
        MatrixXd result;
        // 分支1：双目运算（加减乘）：参数个数=4
        if (argc == 4) {
            string op = argv[2];
            MatrixXd mat1 = parse_matrix(argv[1]);
            MatrixXd mat2 = parse_matrix(argv[3]);

            if (op == "+") {
                result = matrix_add(mat1, mat2);
            } else if (op == "-") {
                result = matrix_subtract(mat1, mat2);
            } else if (op == "*") {
                result = matrix_multiply(mat1, mat2);
            } else {
                cerr << "Invalid operation: " << op << "\n";
                return 1;
            }
        }
        // 分支2：单目运算（求逆、转置）：参数个数=3
        else if (argc == 3) {
            string op = argv[1];
            MatrixXd mat = parse_matrix(argv[2]);

            if (op == "inv") {
                result = matrix_inverse(mat);
            } else if (op == "tran") {
                result = matrix_transpose(mat);
            } else {
                cerr << "Invalid operation: " << op << "\n";
                return 1;
            }
        }
        // 参数数量错误
        else {
            cerr << "Usage: \n";
            cerr << "  matrix_calc <mat1> <op> <mat2>  (+, -, *)\n";
            cerr << "  matrix_calc <op> <mat1>         (inv, tran)\n";
            return 1;
        }

        // 输出结果
        cout << "\nResult Matrix:\n" << result << endl;
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// 矩阵加法
MatrixXd matrix_add(const MatrixXd& a, const MatrixXd& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) {
        throw invalid_argument("Matrix addition requires same dimensions!");
    }
    return a + b;
}

// 矩阵减法
MatrixXd matrix_subtract(const MatrixXd& a, const MatrixXd& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) {
        throw invalid_argument("Matrix subtraction requires same dimensions!");
    }
    return a - b;
}

// 矩阵乘法
MatrixXd matrix_multiply(const MatrixXd& a, const MatrixXd& b) {
    if (a.cols() != b.rows()) {
        throw invalid_argument("Matrix multiplication: cols of A != rows of B!");
    }
    return a * b;
}

// 矩阵求逆（仅方阵）
MatrixXd matrix_inverse(const MatrixXd& a)
{
    if (a.rows() != a.cols()) {
        throw invalid_argument("Matrix inverse only supports square matrix!");
    }
    if (abs(a.determinant()) < 1e-10) {
        throw invalid_argument("Matrix is singular, cannot invert!");
    }
    return a.inverse();
}

// 矩阵转置
MatrixXd matrix_transpose(const MatrixXd& a)
{
    return a.transpose();
}

// 辅助函数：解析命令行矩阵字符串（格式：行,列:元素1,元素2,...）
MatrixXd parse_matrix(const string& mat_str) {
    try {
        // 分割维度和元素
        size_t colon_pos = mat_str.find(':');
        if (colon_pos == string::npos) {
            throw invalid_argument("Invalid matrix format! Use: rows,cols:val1,val2,...");
        }

        string dim_str = mat_str.substr(0, colon_pos);
        string val_str = mat_str.substr(colon_pos + 1);

        // 解析行列
        size_t comma_pos = dim_str.find(',');
        int rows = stoi(dim_str.substr(0, comma_pos));
        int cols = stoi(dim_str.substr(comma_pos + 1));

        // 解析元素
        MatrixXd mat(rows, cols);
        size_t pos = 0;
        int idx = 0;
        while ((pos = val_str.find(',')) != string::npos) {
            mat(idx / cols, idx % cols) = stod(val_str.substr(0, pos));
            val_str.erase(0, pos + 1);
            idx++;
        }
        mat(idx / cols, idx % cols) = stod(val_str);

        if (idx + 1 != rows * cols) {
            throw invalid_argument("Matrix element count does not match dimensions!");
        }
        return mat;
    } catch (...) {
        throw invalid_argument("Failed to parse matrix: " + mat_str);
    }
}
