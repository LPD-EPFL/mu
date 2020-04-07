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

python3 build.py -c $COMPILER clean
python3 build.py -c $COMPILER all