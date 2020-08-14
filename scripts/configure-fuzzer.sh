#!/bin/bash
FUZZER_FLAGS="-fsanitize=fuzzer-no-link,address,shift,integer -fno-sanitize-recover=shift,integer" # ,undefined
export CFLAGS="$CFLAGS $FUZZER_FLAGS"
export CXXFLAGS="$CXXFLAGS $FUZZER_FLAGS"
export CXX=clang-7
export CC=clang-7

CONFIGURE_ARGS="$CONFIGURE_ARGS --disable-go"
CONFIGURE_ARGS="$CONFIGURE_ARGS --enable-libfuzzer=-fsanitize=fuzzer"
exec ./configure $CONFIGURE_ARGS

export TSAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-7
export MSAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-7
export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-7
