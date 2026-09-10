#include <iostream>
#include <iomanip>
#include "CoordStruct.h"
#include "CoordConvert.h"

using namespace std;

int main() {
    // 定义变量
    double rx, ry, rz, sx, sy, sz;

    cout << "\n=========================================================" << endl;
    cout << "           ECEF to ENU Coordinate Transformation           " << endl;
    cout << "=========================================================" << endl;

    cout << "\nPlease enter Station XYZ (ECEF):" << endl;
    cout << "X Y Z: ";
    cin >> rx >> ry >> rz;

    cout << "\nPlease enter Satellite XYZ (ECEF):" << endl;
    cout << "X Y Z: ";
    cin >> sx >> sy >> sz;

    // 构造坐标
    XYZ refXYZ(rx, ry, rz);
    XYZ satXYZ(sx, sy, sz);
    WGS84 frame;

    // ====================== 正变换：ECEF → ENU ======================
    Eigen::Vector3d enu = ecef2enu(refXYZ, satXYZ, frame);
    double elev = elevation(refXYZ, satXYZ);
    double az   = azimuth(refXYZ, satXYZ);

    // ====================== 输出结果 ======================
    cout << fixed << setprecision(3);
    cout << "\n=========================================================" << endl;
    cout << "                        RESULTS                        " << endl;
    cout << "=========================================================" << endl;

    cout << "\nENU Coordinates:" << endl;
    cout << "E: " << enu[0] << " m" << endl;
    cout << "N: " << enu[1] << " m" << endl;
    cout << "U: " << enu[2] << " m" << endl;

    cout << "\nElevation : " << elev << " deg" << endl;
    cout << "Azimuth   : " << az   << " deg" << endl;

    // ====================== 反变换验证：ENU → ECEF ======================
    cout << fixed << setprecision(4);
    XYZ satXYZ_check = enu2ecef(refXYZ, enu, frame);

    cout << "\n=========================================================" << endl;
    cout << "                 INVERSE TRANSFORM CHECK                " << endl;
    cout << "=========================================================" << endl;

    cout << "\nRecovered Satellite ECEF:" << endl;
    cout << "X: " << satXYZ_check[0] << endl;
    cout << "Y: " << satXYZ_check[1] << endl;
    cout << "Z: " << satXYZ_check[2] << endl;

    cout << "\nOriginal Satellite ECEF:" << endl;
    cout << "X: " << satXYZ[0] << endl;
    cout << "Y: " << satXYZ[1] << endl;
    cout << "Z: " << satXYZ[2] << endl;

    cout << "\n=========================================================" << endl;
    cout << "              Transformation Completed!                 " << endl;
    cout << "=========================================================\n" << endl;

    return 0;
}