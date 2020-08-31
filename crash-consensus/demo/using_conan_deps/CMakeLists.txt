cmake_minimum_required( VERSION 3.9 )
project( demo CXX )

include( CheckIPOSupported )
check_ipo_supported(RESULT supported OUTPUT error)

if( supported )
    set( CMAKE_INTERPROCEDURAL_OPTIMIZATION ${DORY_LTO} )
else()
    message( STATUS "IPO / LTO not supported: <${error}>" )
endif()

set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CONAN_SYSTEM_INCLUDES On )

include( ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake )
conan_basic_setup()

MESSAGE( STATUS "CMAKE_C_FLAGS: " ${CMAKE_C_FLAGS} )
MESSAGE( STATUS "CMAKE_CXX_FLAGS: " ${CMAKE_CXX_FLAGS} )
MESSAGE( STATUS "CMAKE_BUILD_TYPE: " ${CMAKE_BUILD_TYPE} )
MESSAGE( STATUS "CMAKE_INTERPROCEDURAL_OPTIMIZATION: " ${CMAKE_INTERPROCEDURAL_OPTIMIZATION} )

set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libgen/exported)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../libgen/exported/include)

find_package( Threads REQUIRED)
find_library(CRASH_CONSENSUS_STATIC NAMES libcrashconsensus.a)
find_library(CRASH_CONSENSUS_SHARED NAMES libcrashconsensus.so)

if (CRASH_CONSENSUS_STATIC)
  set(CRASH_CONSENSUS ${CRASH_CONSENSUS_STATIC})
elseif(CRASH_CONSENSUS_SHARED)
  set(CRASH_CONSENSUS ${CRASH_CONSENSUS_SHARED})
else()
  message(FATAL_ERROR "crash-consensus library not found")
endif()

set(CRASH_CONSENSUS ${CRASH_CONSENSUS} ${CONAN_LIBS} ${CMAKE_THREAD_LIBS_INIT})
add_subdirectory(src)
