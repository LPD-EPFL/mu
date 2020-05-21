cmake_minimum_required(VERSION 2.8)
project(crash-consensus-demo CXX)

add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../exported)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../exported/include)

add_executable(main main.cpp)
target_link_libraries(main consensus memcached pthread ibverbs)

add_executable(stats stats.cpp)
add_executable(fifo fifo.cpp)
