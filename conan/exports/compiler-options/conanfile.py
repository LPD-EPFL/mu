from conans import ConanFile
from compileroptions import *

def set_options(cmake):
    compiler = cmake.definitions["CONAN_COMPILER"].upper()
    build_type = cmake.definitions["CMAKE_BUILD_TYPE"].upper()

    cmake.definitions["CMAKE_C_FLAGS"] = " ".join(
        options["GENERAL"] + options["LANG"]["C"] + options["TARGET"][compiler]["C"]
    )
    cmake.definitions["CMAKE_CXX_FLAGS"] = " ".join(
        options["GENERAL"] + options["LANG"]["CXX"] + options["TARGET"][compiler]["CXX"]
    )
    cmake.definitions["CMAKE_CXX_FLAGS_{}".format(build_type)] = " ".join(
        options["BUILD_TYPE"][build_type]
    )
    cmake.definitions["CMAKE_C_FLAGS_{}".format(build_type)] = " ".join(
        options["BUILD_TYPE"][build_type]
    )
    cmake.definitions["CMAKE_EXE_LINKER_FLAGS"] = options["TARGET"][compiler]["LINKER"]

def generator():
    # The default is None
    # return None
    # return "Unix Makefiles"
    return "Ninja"

class CompilerOptions(ConanFile):
    name = "dory-compiler-options"
    version = "0.0.1"
    exports = "compileroptions.py"
