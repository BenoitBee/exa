#!/bin/sh

../../../../emscripten/emcc src/pipe.c ../common/circular_buffer.c -o exa/pipe.js -I../include -I../common -sASYNCIFY -sTOTAL_MEMORY=1MB -sTOTAL_STACK=128kB -O3
