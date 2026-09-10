#!/usr/bin/env bash
#
# Configure and build GnssLab.
#
# Wraps CMake with the two things it does not handle itself on a typical
# developer machine:
#
#   * CMake is often not on PATH. A JetBrains IDE bundles its own copy and never
#     exposes it, so "cmake" alone frequently fails on a machine where the
#     toolchain is otherwise perfectly usable.
#   * Ninja is likewise bundled rather than installed.
#
# Override either with CMAKE_EXE / NINJA_EXE, or pass CMake arguments through.
#
# Usage:
#   scripts/build.sh                 # Release into build/, all targets
#   scripts/build.sh --clean         # remove build/ first
#   scripts/build.sh --debug         # Debug build into build-debug/
#   scripts/build.sh --target spp_if # one target
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"

BUILD_TYPE="Release"
BUILD_DIR="$ROOT/build"
TARGET=""
CLEAN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)   CLEAN=1 ;;
        --debug)   BUILD_TYPE="Debug"; BUILD_DIR="$ROOT/build-debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --target)  TARGET="${2:-}"; shift ;;
        -h|--help) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# --- locate cmake ----------------------------------------------------------
CMAKE="${CMAKE_EXE:-}"
if [ -z "$CMAKE" ] || [ ! -x "$CMAKE" ]; then
    CMAKE="$(command -v cmake 2>/dev/null || true)"
fi
if [ -z "$CMAKE" ]; then
    # JetBrains IDE bundles, newest version last so this picks the newest.
    for c in "/d/CLion "*/bin/cmake/win/x64/bin/cmake.exe \
             "/c/Program Files/JetBrains/"*/bin/cmake/win/x64/bin/cmake.exe \
             "/c/Program Files/CMake/bin/cmake.exe" \
             "/usr/local/bin/cmake" "/opt/homebrew/bin/cmake"; do
        [ -x "$c" ] && CMAKE="$c"
    done
fi
if [ -z "$CMAKE" ]; then
    echo "error: CMake not found." >&2
    echo "  Install it, or set CMAKE_EXE to its full path." >&2
    exit 1
fi
echo "cmake: $CMAKE"

# --- locate ninja ----------------------------------------------------------
NINJA="${NINJA_EXE:-}"
if [ -z "$NINJA" ] || [ ! -x "$NINJA" ]; then
    NINJA="$(command -v ninja 2>/dev/null || true)"
fi
if [ -z "$NINJA" ]; then
    for c in "/d/CLion "*/bin/ninja/win/x64/ninja.exe \
             "/c/Program Files/JetBrains/"*/bin/ninja/win/x64/ninja.exe \
             "/usr/local/bin/ninja" "/opt/homebrew/bin/ninja"; do
        [ -x "$c" ] && NINJA="$c"
    done
fi

if [ "$CLEAN" = "1" ]; then
    echo "cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

GEN_ARGS=()
if [ -n "$NINJA" ]; then
    echo "ninja: $NINJA"
    GEN_ARGS=(-G Ninja "-DCMAKE_MAKE_PROGRAM=$NINJA")
else
    echo "ninja: not found, letting CMake choose a generator"
fi

echo
"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    "${GEN_ARGS[@]}" \
    "${@}"

echo
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
if [ -n "$TARGET" ]; then
    "$CMAKE" --build "$BUILD_DIR" --target "$TARGET" -j "$JOBS"
else
    "$CMAKE" --build "$BUILD_DIR" -j "$JOBS"
fi

echo
echo "Binaries in $BUILD_DIR/bin:"
ls -1 "$BUILD_DIR/bin" 2>/dev/null || true
echo
echo "Next:"
echo "  python tests/test_baseline_numerics.py      # no data needed"
echo "  python tests/test_regression_pipeline.py    # needs data/sample/"
