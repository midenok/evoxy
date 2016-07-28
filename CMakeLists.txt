cmake_minimum_required(VERSION 3.2)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(AutoOpts REQUIRED)
find_package(LibEV REQUIRED)
find_package(Threads REQUIRED)

add_executable(
    evoxy
    main.cc
    http.cc
    connection.cc
    threads.cc
    cache.cc)

target_autoopts(evoxy evoxy.def)
target_link_libraries(
    evoxy
    "${AUTOOPTS_LIBRARIES}"
    Threads::Threads
    "${LIBEV_LDFLAGS}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 ${AUTOOPTS_CFLAGS} ${LIBEV_CFLAGS}")



## Crutches ##

if (CYGWIN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--image-base -Wl,0x10000000" )
endif (CYGWIN)

add_subdirectory(test)
