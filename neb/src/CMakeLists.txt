cmake_minimum_required( VERSION 3.9 )
project( doryneb CXX )

include( CheckIPOSupported )
check_ipo_supported(RESULT supported OUTPUT error)

if( supported )
    set( CMAKE_INTERPROCEDURAL_OPTIMIZATION ${DORY_LTO} )
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CONAN_SYSTEM_INCLUDES On )


include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_definitions(-DSPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL})

MESSAGE( STATUS "CMAKE_C_FLAGS: " ${CMAKE_C_FLAGS} )
MESSAGE( STATUS "CMAKE_CXX_FLAGS: " ${CMAKE_CXX_FLAGS} )
MESSAGE( STATUS "CMAKE_BUILD_TYPE: " ${CMAKE_BUILD_TYPE} )
MESSAGE( STATUS "CMAKE_INTERPROCEDURAL_OPTIMIZATION: " ${CMAKE_INTERPROCEDURAL_OPTIMIZATION} )


if( DEFINED BUILD_EXECUTABLE )
    if ( ${BUILD_EXECUTABLE} STREQUAL "SYNC" )
        add_executable(
            main
            sync/buffer_overlay.cpp
            sync/broadcast.cpp
            sync/main.cpp
        )        
    elseif( ${BUILD_EXECUTABLE} STREQUAL "ASYNC" )
        add_executable(
            main
            async/buffer_overlay.cpp
            async/broadcast.cpp
            async/main.cpp
        )
    endif()

    find_library( DALEK_FFI ed25519_dalek_ffi HINTS ${CMAKE_LIBRARY_PATH} )
    
    message( STATUS "DALEK_FFI: " ${DALEK_FFI} )
    
    target_link_libraries( main ${CONAN_LIBS} dl pthread ${DALEK_FFI} )

else()
    add_library(
        doryneb
        async.cpp
        async/broadcast.cpp
        async/buffer_overlay.cpp
        sync.cpp
        sync/broadcast.cpp
        sync/buffer_overlay.cpp
    )
endif()


