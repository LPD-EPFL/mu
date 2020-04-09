cmake_minimum_required(VERSION 2.8.11)
project(crypto)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_library(
    crypto
    sign.cpp
)
