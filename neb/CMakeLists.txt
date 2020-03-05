cmake_minimum_required(VERSION 2.8)
project(neb CXX)

add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(
    main 
    buffer_overlay.cpp
    ctrl_block.cpp
    store_conn.cpp
    util.cpp
    neb.cpp
    main.cpp
)

target_link_libraries(main ${CONAN_LIBS} pthread ibverbs memcached)
