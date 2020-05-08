cmake_minimum_required(VERSION 2.8)
project(crypto-bench CXX)

add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()


add_executable(main main.cpp)

target_link_libraries(main ${CONAN_LIBS} pthread)
