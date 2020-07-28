#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

if sys.version_info[0] < 3:
    raise Exception("Must be using Python 3")

import subprocess
import os

current_dir = os.path.dirname(os.path.abspath(__file__))

make_list = subprocess.check_output(["make", "list"], cwd=current_dir)
make_list = make_list.decode("utf-8").split()
make_list = [x.strip() for x in filter(lambda x: not x.endswith("mangled"), make_list)]


def removeFromList(lst, value):
    try:
        lst.remove(value)
        return [value]
    except ValueError:
        return []


# Bring all, clean, distclean in front and sort the rest
removed = (
    removeFromList(make_list, "all")
    + removeFromList(make_list, "clean")
    + removeFromList(make_list, "distclean")
)

# Hide some internal targets
removeFromList(make_list, "compiler-options")
removeFromList(make_list, "make_args")

make_list.sort()
make_list = removed + make_list


import argparse

parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)

parser.add_argument(
    "-b",
    "--build-type",
    choices=["debug", "release", "relwithdebinfo", "minsizerel"],
    default="release",
    nargs="?",
    dest="build_type",
    help="Type of build (default: release)",
)

parser.add_argument(
    "-c",
    "--compiler",
    choices=["gcc", "clang"],
    default="gcc",
    nargs="?",
    dest="compiler",
    help="Type of the compiler (default: gcc)",
)

descr = """What targets to build (default: all)
Possible targets:
{}
""".format(
    "\n".join(make_list)
)

parser.add_argument(
    "TARGET", default="all", nargs="*", choices=make_list, metavar="TARGET", help=descr
)

results = parser.parse_args()

compiler = results.compiler
build_type = results.build_type

if isinstance(results.TARGET, str):
    targets = [results.TARGET]
else:
    targets = list(results.TARGET)


if compiler == "gcc":
    CONAN_PROFILE = os.path.join(
        current_dir, "conan/profiles", "gcc-{}.profile".format(build_type)
    )
    CC = "gcc"
    CXX = "g++"

elif compiler == "clang":
    CONAN_PROFILE = os.path.join(
        current_dir, "conan/profiles", "clang-{}.profile".format(build_type)
    )
    CC = "clang"
    CXX = "clang++"

# The compilers need to be explicity set, as conan won't interfere with the
# build system. Therefore, make sure the CC/CXX flags match the conan profile.
ret = subprocess.call(
    "make CONAN_PROFILE={} CC={} CXX={} {}".format(
        CONAN_PROFILE, CC, CXX, " ".join(targets)
    ),
    cwd=current_dir,
    shell=True,
)

exit(ret)
