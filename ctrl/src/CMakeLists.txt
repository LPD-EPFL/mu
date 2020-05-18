cmake_minimum_required(VERSION 3.10)
project(ctrl)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_definitions(-DSPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL})

add_library(
    ctrl
    device.cpp
    block.cpp
)
