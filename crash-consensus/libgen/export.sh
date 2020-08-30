#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $DIR

LIB_NAME=libcrashconsensus
PROFILES_DIR=$DIR/../../conan/profiles

profiles=()
for profile_path in ../../conan/profiles/*.profile; do
    profile_name=$(basename $profile_path)
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

PROFILE=$1
export CONAN_DEFAULT_PROFILE_PATH=$PROFILES_DIR/${PROFILE}.profile

rm -rf $DIR/build $DIR/exported
mkdir $DIR/build $DIR/exported

conan install $DIR/.. -o shared=False --build=outdated --install-folder=$DIR/build/static/installation
conan build $DIR/.. --build-folder=$DIR/build/static/installation
conan package $DIR/.. --build-folder=$DIR/build/static/installation --package-folder=$DIR/build/static/packaging

cp -r $DIR/build/static/packaging/include $DIR/exported
cp $DIR/build/static/installation/deps/* $DIR/build/static/packaging/lib/

pushd $DIR/build/static
mkdir archiving
pushd archiving
for dep in `ls ../packaging/lib`
do
    dep_dir="${dep%.*}"
    mkdir $dep_dir
    cp ../packaging/lib/$dep $dep_dir
    pushd $dep_dir
    ar x $dep
    rm $dep
    popd
    for object in `ls $dep_dir`
    do
        mv $dep_dir/$object ${dep_dir}_${object}
    done
    rm -r $dep_dir
done

ar rcs $LIB_NAME.a  *.o
popd
popd

mv $DIR/build/static/archiving/$LIB_NAME.a exported


conan install $DIR -o shared=True --build=outdated --install-folder=$DIR/build/shared/installation
conan build $DIR --build-folder=$DIR/build/shared/installation
conan package $DIR --build-folder=$DIR/build/shared/installation --package-folder=$DIR/build/shared/packaging
mv $DIR/build/shared/packaging/lib/$LIB_NAME.so $DIR/exported

rm -rf $DIR/build
