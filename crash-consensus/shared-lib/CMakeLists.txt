cmake_minimum_required(VERSION 3.10)
project(crash-consensus-lib CXX)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
add_compile_options(-std=c++17 -g -O3 -Wall -Wextra -Wpedantic -Werror -Wno-unused-result)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(
    LIB_SRCS
    main.cpp
)

add_library(consensus SHARED ${LIB_SRCS})
target_link_libraries(consensus ${CONAN_LIBS})

# add_library(mergedconsensus-static STATIC ${LIB_SRCS})
# target_link_libraries(mergedconsensus-static ${CONAN_LIBS})
# set_target_properties(mergedconsensus-static PROPERTIES OUTPUT_NAME mergedconsensus)
