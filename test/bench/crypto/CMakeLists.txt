cmake_minimum_required( VERSION 2.8 )
project( crypto-bench CXX )

add_compile_options(-std=c++17 -g -ggdb3 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

set( CONAN_SYSTEM_INCLUDES On )

include( ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake )
conan_basic_setup()


add_executable( main main.cpp )

find_library( DALEK_FFI ed25519_dalek_ffi HINTS ${CMAKE_LIBRARY_PATH} )

message( STATUS "DALEK_FFI: " ${DALEK_FFI} ) 
message( STATUS "CONAN_LIBS: " ${CONAN_LIBS} )
target_link_libraries( main ${CONAN_LIBS} dl ${DALEK_FFI} )
