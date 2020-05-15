cmake_minimum_required(VERSION 3.10)
project(memcached-latency-tests)

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-result)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

add_definitions(-D_GNU_SOURCE)
set(CMAKE_BUILD_TYPE "Release")

add_executable(memcached-puts-only memcached_latency_puts.c)
target_link_libraries(memcached-puts-only Threads::Threads)

add_executable(memcached-gets-only memcached_latency_gets.c)
target_link_libraries(memcached-gets-only Threads::Threads)

add_executable(memcached-puts-gets memcached_latency_puts_gets.c)
target_link_libraries(memcached-puts-gets Threads::Threads)
