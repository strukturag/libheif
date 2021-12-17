#!/bin/bash
set -e
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

CORES=$(nproc --all)
echo "Build using ${CORES} CPU cores"

LIBDE265_VERSION=1.0.8
[ -s "libde265-${LIBDE265_VERSION}.tar.gz" ] || curl \
    -L \
    -o libde265-${LIBDE265_VERSION}.tar.gz \
    https://github.com/strukturag/libde265/releases/download/v${LIBDE265_VERSION}/libde265-${LIBDE265_VERSION}.tar.gz
if [ ! -s "libde265-${LIBDE265_VERSION}/libde265/.libs/libde265.so" ]; then
    tar xf libde265-${LIBDE265_VERSION}.tar.gz
    cd libde265-${LIBDE265_VERSION}
    [ -x configure ] || ./autogen.sh
    emconfigure ./configure --disable-sse --disable-dec265 --disable-sherlock265
    emmake make -j${CORES}
    cd ..
fi

CONFIGURE_ARGS="--disable-multithreading --disable-go --disable-examples"

emconfigure ./configure $CONFIGURE_ARGS \
    PKG_CONFIG_PATH="${DIR}/libde265-${LIBDE265_VERSION}" \
    libde265_CFLAGS="-I${DIR}/libde265-${LIBDE265_VERSION}" \
    libde265_LIBS="-L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs"
if [ ! -e "Makefile" ]; then
    # Most likely the first run of "emscripten" which will generate the
    # config file and terminate. Run "emconfigure" again.
    emconfigure ./configure $CONFIGURE_ARGS \
        PKG_CONFIG_PATH="${DIR}/libde265-${LIBDE265_VERSION}" \
        libde265_CFLAGS="-I${DIR}/libde265-${LIBDE265_VERSION}" \
        libde265_LIBS="-L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs"
fi
emmake make -j${CORES}

export TOTAL_MEMORY=16777216

echo "Running Emscripten..."
emcc libheif/.libs/libheif.so \
        --bind \
        -s WASM=0 \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s MODULARIZE=1 \
        -s EXPORT_NAME="libheif" \
        --memory-init-file 0 \
        -O3 \
        -L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs \
        -lde265 \
        -o libheif.js
