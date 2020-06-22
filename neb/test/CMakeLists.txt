cmake_minimum_required( VERSION 3.9 )
project( dorynebtest CXX )

include( CheckIPOSupported )
check_ipo_supported(RESULT supported OUTPUT error)

if( supported )
    set( CMAKE_INTERPROCEDURAL_OPTIMIZATION ${DORY_LTO} )
    if ( ${DORY_LTO} AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
        set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld" )
    endif()
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CONAN_SYSTEM_INCLUDES On )


include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(
    main
    main.cpp
)

target_link_libraries( main ${CONAN_LIBS} )

