cmake_minimum_required(VERSION 2.8)
project(leader-election CXX)

add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(
    LIB_SRCS
    memory.cpp
    log/log-iterators.cpp
    log/log.cpp

    response-tracker.cpp
    quorum-waiter.cpp
    pinning.cpp
    consensus.cpp
)

add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# add_library(rdmaconsensus SHARED ${LIB_SRCS})
# target_link_libraries(rdmaconsensus ${CONAN_LIBS} pthread)

add_library(rdmaconsensus STATIC ${LIB_SRCS})
target_link_libraries(rdmaconsensus ${CONAN_LIBS} pthread)

add_executable(main main.cpp)
target_link_libraries(main rdmaconsensus)

add_executable(stats stats.cpp)
