#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
if sys.version_info[0] < 3:
    raise Exception("Must be using Python 3")

import subprocess
import os

make_list = subprocess.check_output(['make', 'list'])
make_list = make_list.decode('utf-8').split()
make_list = [x.strip() for x in filter(lambda x: not x.endswith('mangled'), make_list)]

def removeFromList(lst, value):
    try:
        lst.remove(value)
        return [value]
    except ValueError:
        return []

# Bring all, clean, distclean in front and sort the rest
removed = removeFromList(make_list, 'all') + removeFromList(make_list,
        'clean') + removeFromList(make_list, 'distclean')
make_list.sort()
make_list = removed + make_list


import argparse
parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)

parser.add_argument(
    '-c',
    '--compiler',
    choices=['gcc', 'clang'],
    default='gcc',
    nargs='?',
    dest='compiler',
    help='Type of the compiler (default: gcc)',
    )

descr = """What targets to build (default: all)
Possible targets:
{}
""".format('\n'.join(make_list))

parser.add_argument('TARGET', default='all', nargs='*',
                    choices=make_list, metavar='TARGET', help=descr)

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
    CXX='g++'

elif compiler == 'clang':
    CONAN_PROFILE = os.path.join(current_dir, 'conan-profiles',
                                 'clang-release.profile')
    CC='clang'
    CXX='clang++'

# The compilers need to be explicity set, as conan won't interfere with the
# build system. Therefore, make sure the CC/CXX flags match the conan profile.
subprocess.call('make CONAN_PROFILE={} CC={} CXX={} {}'.format(CONAN_PROFILE,
               CC, CXX, ' '.join(targets)), shell=True)
