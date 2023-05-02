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

CONFIGURE_ARGS="-DENABLE_MULTITHREADING_SUPPORT=OFF -DWITH_GDK_PIXBUF=OFF -DWITH_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON"
#export PKG_CONFIG_PATH="${DIR}/libde265-${LIBDE265_VERSION}"

emcmake cmake $CONFIGURE_ARGS \
    -DLIBDE265_INCLUDE_DIR="${DIR}/libde265-${LIBDE265_VERSION}" \
    -DLIBDE265_LIBRARY="-L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs"
emmake make -j${CORES}

export TOTAL_MEMORY=16777216

echo "Running Emscripten..."
emcc libheif/libheif.so \
     --bind \
     --closure 0 \
    -s NO_EXIT_RUNTIME=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ASSERTIONS=0 \
    -s INVOKE_RUN=0 \
    -s DOUBLE_MODE=0 \
    -s PRECISE_F32=0 \
    -s DISABLE_EXCEPTION_CATCHING=1 \
    -s USE_CLOSURE_COMPILER=0 \
    -s LEGACY_VM_SUPPORT=1 \
    --memory-init-file 0 \
    -O3 \
    -std=c++11 \
    -L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs \
    -lde265 \
    --pre-js pre.js \
    --post-js post.js \
    -o libheif.js
