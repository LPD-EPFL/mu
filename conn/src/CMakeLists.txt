cmake_minimum_required(VERSION 2.8.11)
project(conn)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_definitions(-DSPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL})

add_library(
    conn
    rc.cpp
    exchanger.cpp
)
