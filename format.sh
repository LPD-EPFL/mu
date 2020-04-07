#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"


if [ $# -eq 0 ]
then
    cd "$DIR" || return
    echo "No arguments supplied, running on the entire source"
    clang-format -i -style=file $(find . \( ! -path "*build*" -name "*.cpp" -o -name "*.hpp" \) -print0 | xargs -0)
else
    echo "Formatting files: ${*:1}"
    clang-format -i -assume-filename=$DIR/.clang-format -style=file ${*:1}
fi
