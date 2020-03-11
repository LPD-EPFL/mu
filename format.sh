#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "$DIR" || return
clang-format -i -style=file $(find . \( ! -path "*build*" -name "*.cpp" -o -name "*.hpp" \) -print0 | xargs -0)
