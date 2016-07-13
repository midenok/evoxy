#!/bin/bash
mkdir -p build
cd build
unset override
if [ -x /bin.override/c++ ]
then
    override="-D CMAKE_CXX_COMPILER:FILEPATH=/bin.override/c++"
fi
if [ -x /bin.override/cc ]
then
    override="$override -D CMAKE_C_COMPILER:FILEPATH=/bin.override/cc"
fi
cmake -D CMAKE_BUILD_TYPE:STRING=Debug $override ..
# make
