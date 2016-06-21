cmake_minimum_required(VERSION 3.2)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable(udtproxy main.cc http.cc connection.cc udtproxy.c threads.cc)
target_link_libraries(udtproxy -lopts -lpthread -lev)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11" )

add_custom_command(OUTPUT udtproxy.c udtproxy.h COMMAND autogen ${CMAKE_CURRENT_SOURCE_DIR}/udtproxy.def MAIN_DEPENDENCY udtproxy.def)
# use `autoopts-config ldflags` instead of -lopts

## Crutches ##

if (CYGWIN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--image-base -Wl,0x10000000" )
endif (CYGWIN)
