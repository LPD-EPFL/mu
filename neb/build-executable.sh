#!/bin/bash
set -x
set -e

# Default log level is info. For benchmarking rather use "WARN" or even "CRITICAL"
LOG_LEVEL="INFO"
TARGET="SYNC"

# ----------------------------- Argument Parsing ----------------------------- #
while [[ $# -gt 0 ]]
do
    key="$1"

    case $key in
        -l|--LOG_LEVEL)
            LOG_LEVEL=$2
            shift
            shift
        ;;
        -t|--TARGET)
            TARGET=$2
            shift
            shift
        ;;
        # default case
        *)
            # save it in an array for later restore
            POSITIONAL+=("$1")
            shift
        ;;
    esac
done
# restore positional parameters
set -- "${POSITIONAL[@]}"
# ---------------------------------------------------------------------------- #

echo "Building TARGET: $TARGET with LOG_LEVEL: $LOG_LEVEL"

rm -rf build
mkdir build
pushd build

conan install .. --build=missing

cmake -G "Unix Makefiles" \
    -DSPDLOG_ACTIVE_LEVEL="SPDLOG_LEVEL_$LOG_LEVEL" -DBUILD_EXECUTABLE="$TARGET" \
    -DCMAKE_BUILD_TYPE="Release" -DCONAN_CMAKE_CXX_STANDARD="17" \
    -DCONAN_CMAKE_CXX_EXTENSIONS="OFF" -DCONAN_STD_CXX_FLAG="-std=c++17" \
    -DCONAN_IN_LOCAL_CACHE="ON" -DCONAN_COMPILER="gcc" \
    -DCONAN_COMPILER_VERSION="7" -DCONAN_CXX_FLAGS="-m64" \
    -DCONAN_SHARED_LINKER_FLAGS="-m64" -DCONAN_C_FLAGS="-m64" \
    -DCONAN_LIBCXX="libstdc++11" -DBUILD_SHARED_LIBS="OFF" \
    -DCMAKE_INSTALL_BINDIR="bin" -DCMAKE_INSTALL_SBINDIR="bin" \
    -DCMAKE_INSTALL_LIBEXECDIR="bin" -DCMAKE_INSTALL_LIBDIR="lib" \
    -DCMAKE_INSTALL_INCLUDEDIR="include" \
    -DCMAKE_INSTALL_OLDINCLUDEDIR="include" -DCMAKE_INSTALL_DATAROOTDIR="share" \
    -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY="ON" -DCONAN_EXPORTED="1" \
    -DCMAKE_C_FLAGS="-g -Werror -Wall -Wconversion -Wfloat-equal -Wpedantic -Wpointer-arith -Wswitch-default -Wpacked -Wextra -Winvalid-pch -Wmissing-field-initializers -Wunreachable-code -Wcast-align -Wcast-qual -Wdisabled-optimization -Wformat=2 -Wformat-nonliteral -Wuninitialized -Wformat-security -Wformat-y2k -Winit-self -Wmissing-declarations -Wmissing-include-dirs -Wredundant-decls -Wstrict-overflow=5 -Wundef -Wno-unused -Wlogical-op" \
    -DCMAKE_CXX_FLAGS="-g -Werror -Wall -Wconversion -Wfloat-equal -Wpedantic -Wpointer-arith -Wswitch-default -Wpacked -Wextra -Winvalid-pch -Wmissing-field-initializers -Wunreachable-code -Wcast-align -Wcast-qual -Wdisabled-optimization -Wformat=2 -Wformat-nonliteral -Wuninitialized -Wformat-security -Wformat-y2k -Winit-self -Wmissing-declarations -Wmissing-include-dirs -Wredundant-decls -Wstrict-overflow=5 -Wundef -Wno-unused -Wctor-dtor-privacy -Wsign-promo -Woverloaded-virtual -Wold-style-cast -Wlogical-op -Wstrict-null-sentinel -Wnoexcept" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" -Wno-dev ../src

cmake --build .
