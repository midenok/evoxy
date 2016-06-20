cmake_minimum_required(VERSION 3.2)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable(udtproxy main.cc http.cc udtproxy.c threads.cc)
target_link_libraries(udtproxy -lopts -lpthread -lev)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11" )
add_custom_command(OUTPUT udtproxy.c udtproy.h COMMAND autogen ${CMAKE_CURRENT_SOURCE_DIR}/udtproxy.def MAIN_DEPENDENCY udtproxy.def)
# use `autoopts-config ldflags` instead of -lopts
