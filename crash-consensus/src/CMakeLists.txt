cmake_minimum_required(VERSION 2.8.11)
project(dorycrashconsensus)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_definitions(-DSPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL})

add_library(
    dorycrashconsensus
    memory.cpp
    log/log-iterators.cpp
    log/log.cpp

    contexted-poller.cpp
    response-tracker.cpp
    quorum-waiter.cpp
    pinning.cpp
    consensus.cpp
    crash-consensus.cpp
    crash-consensus-c-wrapper.cpp
)
