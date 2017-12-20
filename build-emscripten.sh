#!/bin/bash
set -e

emconfigure ./configure
if [ ! -e "Makefile" ]; then
    # Most likely the first run of "emscripten" which will generate the
    # config file and terminate. Run "emconfigure" again.
    emconfigure ./configure
fi
emmake make

export TOTAL_MEMORY=8388608

export LIBRARY_FUNCTIONS="[ \
    'memcpy', \
    'memset', \
    'malloc', \
    'free'
]"

echo "Running Emscripten..."
emcc src/.libs/libheif.so \
    --bind \
    -s NO_EXIT_RUNTIME=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ASSERTIONS=0 \
    -s INVOKE_RUN=0 \
    -s PRECISE_I32_MUL=0 \
    -s DISABLE_EXCEPTION_CATCHING=1 \
    -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE="${LIBRARY_FUNCTIONS}" \
    -O3 \
    -std=c++11 \
    --pre-js pre.js \
    --post-js post.js \
    -o libheif.js
