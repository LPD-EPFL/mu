#!/bin/bash

# This file is sourced from the build scripts

parse_profile () {
    local DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
    cd $DIR
    
    local PROFILES_DIR=$DIR/../../conan/profiles
    
    local profiles=()
    for profile_path in $PROFILES_DIR/*.profile; do
        local profile_name=$(basename $profile_path)
        profiles+=(${profile_name%.*})
    done

    help() {
        echo "Usage : $0 <conan-profile>"
        echo ""
        echo "<conan-profile> can be one of the following:"
        printf '\t\t%s\n' "${profiles[@]}"
    }

    if [ $# -lt 1 ]
    then
        help
        exit 1
    fi
    
    if [[ ! " ${profiles[@]} " =~ " $1 " ]]; then
        help
        exit 1
    fi
    
    local PROFILE=$1
    export CONAN_DEFAULT_PROFILE_PATH=$PROFILES_DIR/${PROFILE}.profile
}

parse_profile "$@"
