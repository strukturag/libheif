#!/bin/bash
set -e

emconfigure ./configure
if [ ! -e "Makefile" ]; then
    # Most likely the first run of "emscripten" which will generate the
    # config file and terminate. Run "emconfigure" again.
    emconfigure ./configure
fi
emmake make

export TOTAL_MEMORY=16777216

echo "Running Emscripten..."
emcc src/.libs/libheif.so \
    --bind \
    -s NO_EXIT_RUNTIME=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
    -s ASSERTIONS=0 \
    -s INVOKE_RUN=0 \
    -s DISABLE_EXCEPTION_CATCHING=1 \
    -s USE_CLOSURE_COMPILER=1 \
    -s LEGACY_VM_SUPPORT=1 \
    --memory-init-file 0 \
    -O3 \
    -std=c++11 \
    --pre-js pre.js \
    --post-js post.js \
    -o libheif.js
