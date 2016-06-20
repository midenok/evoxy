#!/bin/bash
mkdir -p build
cd build
cmake -D CMAKE_BUILD_TYPE:STRING=Debug -D CMAKE_CXX_COMPILER:FILEPATH=/bin.override/c++ -D CMAKE_C_COMPILER:FILEPATH=/bin.override/cc ..
# make
