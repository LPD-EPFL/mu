add_executable(main-st main-st.cpp)
target_link_libraries(main-st ${CRASH_CONSENSUS})

add_executable(main-st-lat main-st-lat.cpp)
target_link_libraries(main-st-lat ${CRASH_CONSENSUS})

add_executable(main-dt main-dt.cpp)
target_link_libraries(main-dt ${CRASH_CONSENSUS})

add_executable(stats stats.cpp)
add_executable(fifo fifo.cpp)
