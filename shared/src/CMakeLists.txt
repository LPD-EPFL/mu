cmake_minimum_required(VERSION 2.8.11)
project(_shared)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_library(
    _shared
    logger.cpp
)
