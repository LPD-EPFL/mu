#!/bin/bash

set -e

function show_help() {
    cat <<_EOF
Desc:
    Creates all dory conan packages and builds the executables

Usage:
    $0 [-h | --help] COMPILER

COMPILER:
    - gcc (default) : uses the default conan profile
    - clang         : uses clang v6.0 with libstdc++11

-h, --help:
    Print this help
_EOF
}

# ----------------------------- Argument Parsing ----------------------------- #
while [[ $# -gt 0 ]]
do
    key="$1"

    case $key in
        # -e|--example)
        #     VAR="$2"
        #     shift # <- key
        #     shift # <- value
        # ;;
        -h|--help)
            show_help
            exit 0;
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

if [ -z "$1" ]; then
    COMPILER="gcc"
else
    COMPILER="$1"
fi

if [ "$COMPILER" == "clang" ]; then
    if [ -z "$CC" ]; then
        echo "CC env variable is unset. Required to be set for cmake."
        echo "Exiting!"
        exit 1;
    fi
    if [ -z "$CXX" ]; then
        echo "CCX env variable is unsed. Required to be set for cmake."
        echo "Exiting!"
        exit 2;
    fi
fi

echo "Building dory source with $COMPILER"

echo "Starting off with creating conan packages"
for p in shared memstore ctrl conn;
do
    pushd "$p"
        if [ "$COMPILER" == "clang" ]; then
            conan create . -s compiler=clang -s compiler.version="6.0" -s compiler.libcxx="libstdc++11"
        else
            conan create .
        fi
    popd
done

echo "Building executables"
for m in leader-election neb;
do
    pushd "$m"
        ./build.sh "$COMPILER"
    popd
done
