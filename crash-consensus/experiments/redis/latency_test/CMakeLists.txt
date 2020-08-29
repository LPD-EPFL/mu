cmake_minimum_required(VERSION 3.10)
project(redis-latency-tests)

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-result)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

add_definitions(-D_GNU_SOURCE)
set(CMAKE_BUILD_TYPE "Release")

add_executable(redis-puts-only redis_latency_puts.c)
target_link_libraries(redis-puts-only Threads::Threads)

add_executable(redis-gets-only redis_latency_gets.c)
target_link_libraries(redis-gets-only Threads::Threads)

add_executable(redis-puts-gets redis_latency_puts_gets.c)
target_link_libraries(redis-puts-gets Threads::Threads)
