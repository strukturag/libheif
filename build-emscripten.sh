#!/bin/bash
set -e
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

CORES=$(nproc --all)
echo "Build using ${CORES} CPU cores"

LIBDE265_VERSION=1.0.12
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

CONFIGURE_ARGS="-DENABLE_MULTITHREADING_SUPPORT=OFF -DWITH_GDK_PIXBUF=OFF -DWITH_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON -DENABLE_PLUGIN_LOADING=OFF"
#export PKG_CONFIG_PATH="${DIR}/libde265-${LIBDE265_VERSION}"

emcmake cmake $CONFIGURE_ARGS \
    -DCMAKE_EXE_LINKER_FLAGS="-lembind -lde265" \
    -DLIBDE265_INCLUDE_DIR="${DIR}/libde265-${LIBDE265_VERSION}" \
    -DLIBDE265_LIBRARY="-L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs"
emmake make -j${CORES}

export TOTAL_MEMORY=16777216

LIBHEIFA="libheif/libheif.a"
#EXPORTED_FUNCTIONS=$($EMSDK/upstream/bin/llvm-nm $LIBHEIFA --format=just-symbols | grep "^heif_\|^de265_" | grep "[^:]$" | sed 's/^/_/' | paste -sd "," -)

echo "Running Emscripten..."
emcc "$LIBHEIFA" \
    -lembind \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s LINKABLE=1 \
    -O0 \
    -std=c++11 \
    -L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs \
    -lde265 \
    --pre-js pre.js \
    --post-js post.js \
    -s MODULARIZE  \
    -s 'EXPORT_NAME="createLibheifModule"' \
    -o libheif.html
