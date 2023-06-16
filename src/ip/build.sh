#!/bin/sh

set -x

../../../../emscripten/emcc src/ip.c -o exa/ip.js -I../include -I../common -sASYNCIFY -sINITIAL_MEMORY=16MB -sTOTAL_STACK=512kB -sASYNCIFY_STACK_SIZE=256000 -sALLOW_MEMORY_GROWTH=1 -O3 "$@"
