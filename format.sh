#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd $DIR
clang-format -i -style=file `find . \( -name "*.cpp" -o -name "*.hpp" \) | xargs`
