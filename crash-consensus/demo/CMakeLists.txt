cmake_minimum_required(VERSION 2.8)
project(crash-consensus-demo CXX)

add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Wno-unused-result)

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../exported)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../exported/include)

add_executable(main-st main-st.cpp)
target_link_libraries(main-st consensus memcached pthread ibverbs)

add_executable(main-st-lat main-st-lat.cpp)
target_link_libraries(main-st-lat consensus memcached pthread ibverbs)

add_executable(main-dt main-dt.cpp)
target_link_libraries(main-dt consensus memcached pthread ibverbs)

add_executable(stats stats.cpp)
add_executable(fifo fifo.cpp)
