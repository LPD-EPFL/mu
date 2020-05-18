cmake_minimum_required(VERSION 3.10)
project(memstore CXX)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_library(memstore store.cpp)
