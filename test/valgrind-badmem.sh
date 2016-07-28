#!/bin/sh
[ -d ../build ] && cd ../build
valgrind \
	--leak-check=no \
	--track-origins=yes \
	--log-file=valgrind-badmem.log \
	./evoxy -v -A 1 "$@"

less valgrind-badmem.log
