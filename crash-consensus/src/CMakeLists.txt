cmake_minimum_required( VERSION 3.9 )
project(dorycrashconsensus)

include( CheckIPOSupported )
check_ipo_supported(RESULT supported OUTPUT error)

if( supported )
    set( CMAKE_INTERPROCEDURAL_OPTIMIZATION ${DORY_LTO} )
else()
    message( STATUS "IPO / LTO not supported: <${error}>" )
endif()

set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CONAN_SYSTEM_INCLUDES On )

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
