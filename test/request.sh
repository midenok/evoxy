#!/bin/sh
export http_proxy=http://localhost:9000/
wget -p http://lenta.ru -O/dev/null
