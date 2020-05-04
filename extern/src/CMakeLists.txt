cmake_minimum_required(VERSION 2.8.11)
project(extern)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

find_library(IBVERBS_LIB ibverbs)
if(NOT IBVERBS_LIB)
     message(FATAL_ERROR "ibverbs library not found")
endif()

find_library(MEMCACHED_LIB memcached)
if(NOT MEMCACHED_LIB)
     message(FATAL_ERROR "memcached library not found")
endif()
