cmake_minimum_required(VERSION 3.2)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(evoxy main.cc http.cc connection.cc evoxy.c threads.cc cache.cc)
target_link_libraries(evoxy -lopts -lpthread -lev)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11" )

add_custom_command(OUTPUT evoxy.c evoxy.h COMMAND autogen ${CMAKE_CURRENT_SOURCE_DIR}/evoxy.def MAIN_DEPENDENCY evoxy.def)
# use `autoopts-config ldflags` instead of -lopts

## Crutches ##

if (CYGWIN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--image-base -Wl,0x10000000" )
endif (CYGWIN)

add_subdirectory(test)
