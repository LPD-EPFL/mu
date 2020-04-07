#!/usr/bin/python3
# -*- coding: utf-8 -*-

import sys
if sys.version_info[0] < 3:
    raise Exception("Must be using Python 3")

import argparse
import subprocess
import os

parser = argparse.ArgumentParser()

parser.add_argument(
    '-c',
    '--compiler',
    choices=['gcc', 'clang'],
    default='gcc',
    nargs='?',
    dest='compiler',
    help='Type of the compiler (default: gcc)',
    )

parser.add_argument('TARGET', default='all', nargs='*',
                    help='What targets to build (default: all)')

results = parser.parse_args()

compiler = 'gcc'
if results.compiler is not None:
    compiler = results.compiler

if isinstance(results.TARGET, str):
    targets = [results.TARGET]
else:
    targets = list(results.TARGET)

current_dir = os.path.dirname(os.path.abspath(__file__))
if compiler == 'gcc':
    CONAN_PROFILE = os.path.join(current_dir, 'conan-profiles',
                                 'gcc-release.profile')
    CC='gcc'
    CXX='c++'

elif compiler == 'clang':
    CONAN_PROFILE = os.path.join(current_dir, 'conan-profiles',
                                 'clang-release.profile')
    CC='clang'
    CXX='clang++'

# The compilers need to be explicity set, as conan won't interfere with the
# build system. Therefore, make sure the CC/CXX flags match the conan profile.
subprocess.call('make CONAN_PROFILE={} CC={} CXX={} {}'.format(CONAN_PROFILE,
               CC, CXX, ' '.join(targets)), shell=True)
