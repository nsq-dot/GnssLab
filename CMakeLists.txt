###################################################################
#
#  GnssLab — CMake configuration
#
#  Derived from gnssLab-2.4 by Shoujian Zhang (Wuhan University).
#  See NOTICE for the full list of modifications.
#
###################################################################

# 3.20 rather than a bleeding-edge floor: this builds on the CMake that ships
# with Ubuntu 22.04/24.04 and on GitHub's ubuntu-latest image. Requiring 4.x
# is a configure-time hard error for most people who would try to build this.
cmake_minimum_required(VERSION 3.20)

project(GnssLab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Extensions stay ON. The sources use M_PI in a couple of places, which is a
# POSIX/glibc extension rather than standard C++. Turning this OFF will break
# the build until those are replaced; see docs/roadmap.md.
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Put every executable in one predictable place so the Python CLI and the CI
# scripts can find them without guessing the generator-specific directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#-------------------------------------------------------------------
# The library
#-------------------------------------------------------------------
# A STATIC archive, deliberately. An earlier revision listed every source file
# separately in each add_executable() to sidestep MinGW shared-library problems
# — but those problems are specific to SHARED targets (DLL export, import
# library, runtime DLL search path). A static archive has none of them, and it
# removes ~110 lines of duplicated source lists that had already drifted out of
# sync between targets.
add_library(gnss STATIC
        src/ARLambda.cpp
        src/CSDetector.cpp
        src/ConfigData.cpp
        src/ConfigReader.cpp
        src/GnssFunc.cpp
        src/GnssStruct.cpp
        src/KalmanFilter.cpp
        src/NavEphBDS.cpp
        src/NavEphGPS.cpp
        src/RinexNavStore.cpp
        src/RinexObsReader.cpp
        src/Rx3ClockReader.cpp
        src/SP3Store.cpp
        src/SPPIFCode.cpp
        src/SPPVelocity.cpp
        src/SolverKalman.cpp
        src/SolverLSQ.cpp
        src/TimeConvert.cpp
        src/TimeStruct.cpp
)

# PUBLIC, so every target that links `gnss` inherits -Isrc and can keep writing
# bare #include "GnssFunc.h". This replaces the old global include_directories().
target_include_directories(gnss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(gnss SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/eigen-3.4.0)

if(MINGW)
    # Link the GCC runtime statically.
    #
    # By default MinGW links libstdc++-6.dll, libgcc_s_seh-1.dll and
    # libwinpthread-1.dll dynamically, so a binary depends on whichever copy of
    # those DLLs happens to come first on PATH. Git for Windows ships its own
    # MinGW runtime in <git>/mingw64/bin, which frequently precedes the compiler's
    # own directory - and the two runtimes are not interchangeable. The failure is
    # nasty to diagnose: an *optimised* build dies before main() with
    # STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139) / a bare segfault and prints
    # nothing, while the same code built -O0 runs fine, which looks like a code
    # bug rather than a deployment one.
    #
    # Static linking removes the dependency entirely and makes the shipped
    # binaries self-contained. Costs a few hundred KB per executable.
    target_link_options(gnss PUBLIC -static-libgcc -static-libstdc++ -static)
endif()

#-------------------------------------------------------------------
# Application programs
#-------------------------------------------------------------------
set(GNSS_APPS
        spp_if            # SPP (dual-frequency ionosphere-free) + Doppler velocity
        system_bias       # TGD / ionospheric / tropospheric diagnostics
        read_rinex        # RINEX inventory and per-constellation statistics
        bds_eph           # broadcast vs precise ephemeris, single satellite
        bds_gps_diff      # broadcast vs precise orbit/clock, full constellation
        cs_detect_mw      # Melbourne-Wübbena cycle-slip detection
        matrix_calculator # Eigen matrix calculator
)

foreach(app ${GNSS_APPS})
    add_executable(${app} apps/${app}.cpp)
    target_link_libraries(${app} PRIVATE gnss)
endforeach()

#-------------------------------------------------------------------
# Teaching examples (interactive, read from stdin)
#-------------------------------------------------------------------
set(GNSS_EXAMPLES
        parse_opt             # command-line argument parsing
        parse_config          # configuration-file parsing
        gpst_to_utc           # GPS time -> UTC
        bdweek_to_commontime  # BeiDou week/second -> CommonTime
        jd2020_test           # BDS week/second -> JD2020, round-trip
        ecef_enu_test         # ECEF <-> ENU, elevation and azimuth
)

foreach(ex ${GNSS_EXAMPLES})
    add_executable(${ex} examples/${ex}.cpp)
    target_link_libraries(${ex} PRIVATE gnss)
endforeach()

# matrix_calculator and parse_opt are self-contained and include nothing from
# src/, so linking them against the library is unnecessary. Harmless, and it
# keeps the target definitions uniform.

#-------------------------------------------------------------------
# Tests
#-------------------------------------------------------------------
enable_testing()

find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
    add_test(NAME baseline_numerics
             COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_baseline_numerics.py)
    set_tests_properties(baseline_numerics PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

#-------------------------------------------------------------------
# Install
#-------------------------------------------------------------------
install(TARGETS gnss ARCHIVE DESTINATION lib)
install(TARGETS ${GNSS_APPS} ${GNSS_EXAMPLES} RUNTIME DESTINATION bin)
