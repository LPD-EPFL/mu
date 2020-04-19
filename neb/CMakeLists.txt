cmake_minimum_required(VERSION 2.8)
project(neb CXX)

add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(
    main
    buffer_overlay.cpp
    neb.cpp
    main.cpp
)

target_link_libraries(main ${CONAN_LIBS} pthread)
