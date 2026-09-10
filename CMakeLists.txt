cmake_minimum_required(VERSION 4.1)
project(GnssLab)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(lib)
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/eigen-3.4.0/)

add_executable(matrix_calculator examples/matrix_calculator.cpp)
add_executable(exam-1.1-parse_opt examples/exam-1.1-parse_opt.cpp)
add_executable(exam-1.2-parse_config lib/ConfigReader.cpp)
add_executable(GPS_to_UTC
        examples/GPST_to_UTCT.cpp   # 你的主程序
        lib/TimeStruct.cpp        # 时间类实现
        lib/TimeConvert.cpp       # 时间转换实现
)
add_executable(BDWeekSecond_to_CommonTime
        examples/BDWeekSecond_to_CommonTime.cpp
        lib/TimeStruct.cpp
        lib/TimeConvert.cpp
)
add_executable(JD2020_Test
        examples/JD2020_Test.cpp
        lib/TimeStruct.cpp
        lib/TimeConvert.cpp
)
add_executable(ECEF_ENU_Test
        examples/ECEF_ENU_Test.cpp
)
add_executable(read_rinex_data
         examples/read_rinex_data.cpp
         lib/GnssStruct.cpp
         lib/TimeConvert.cpp
         lib/GnssFunc.cpp
         lib/RinexNavStore.cpp
         lib/RinexObsReader.cpp
         lib/SPPIFCode.cpp
         lib/NavEphGPS.cpp
         lib/ARLambda.cpp
         lib/TimeStruct.cpp
         lib/SolverLSQ.cpp
         lib/NavEphBDS.cpp
)
add_executable(bds_eph
         examples/bds_eph.cpp
         lib/TimeConvert.cpp
         lib/TimeStruct.cpp
         lib/GnssStruct.cpp
         lib/NavEphBDS.cpp
         lib/Rx3ClockReader.cpp
         lib/SP3Store.cpp
         lib/RinexNavStore.cpp
         lib/NavEphGPS.cpp
)
add_executable(BDS_GPS_diff
        examples/BDS_GPS_diff.cpp
        lib/TimeConvert.cpp
        lib/TimeStruct.cpp
        lib/GnssStruct.cpp
        lib/NavEphBDS.cpp
        lib/Rx3ClockReader.cpp
        lib/SP3Store.cpp
        lib/RinexNavStore.cpp
        lib/NavEphGPS.cpp
)
add_executable(system_bias
         examples/system_bias.cpp
         lib/GnssStruct.cpp
         lib/TimeConvert.cpp
         lib/TimeStruct.cpp
         lib/GnssFunc.cpp
         lib/RinexNavStore.cpp
         lib/RinexObsReader.cpp
         lib/NavEphGPS.cpp
         lib/NavEphBDS.cpp
         lib/SolverLSQ.cpp
         lib/ARLambda.cpp
         lib/ConfigReader.cpp
)
add_executable(sppif
        examples/sppif.cpp
        lib/GnssStruct.cpp
        lib/TimeConvert.cpp
        lib/TimeStruct.cpp
        lib/GnssFunc.cpp
        lib/RinexNavStore.cpp
        lib/RinexObsReader.cpp
        lib/SPPIFCode.cpp
        lib/SPPVelocity.cpp
        lib/NavEphGPS.cpp
        lib/NavEphBDS.cpp
        lib/SolverLSQ.cpp
        lib/ARLambda.cpp
        lib/ConfigReader.cpp
)
add_executable(cs_detect_mw
        examples/cs_detect_mw.cpp
        lib/GnssStruct.cpp
        lib/TimeConvert.cpp
        lib/TimeStruct.cpp
        lib/GnssFunc.cpp
        lib/RinexNavStore.cpp
        lib/RinexObsReader.cpp
        lib/SPPIFCode.cpp
        lib/SPPVelocity.cpp
        lib/NavEphGPS.cpp
        lib/NavEphBDS.cpp
        lib/SolverLSQ.cpp
        lib/ARLambda.cpp
        lib/ConfigReader.cpp
)