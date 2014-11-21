cmake_minimum_required (VERSION 3.0)
project (examples)

set(BOOST boost_system boost_coroutine boost_context)

add_definitions(-g -Wall -std=c++1y -pthread )

add_executable(traditional traditional.cpp)
target_link_libraries(traditional pthread ${BOOST})

add_executable(async async.cpp)
target_link_libraries(async pthread ${BOOST})

add_executable(modern modern.cpp)
target_link_libraries(modern pthread ${BOOST})
