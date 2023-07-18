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


AOM_VERSION=3.6.1
[ -s "aom-${AOM_VERSION}.tar.gz" ] || curl \
    -L \
    -o aom-${AOM_VERSION}.tar.gz \
    "https://aomedia.googlesource.com/aom/+archive/v${AOM_VERSION}.tar.gz"
if [ ! -s "aom-${AOM_VERSION}/libaom.a" ]; then
    mkdir -p aom-${AOM_VERSION}/aom-source
    tar xf aom-${AOM_VERSION}.tar.gz -C aom-${AOM_VERSION}/aom-source
    cd aom-${AOM_VERSION}

    emcmake cmake aom-source \
        -DENABLE_CCACHE=1 \
        -DAOM_TARGET_CPU=generic \
        -DENABLE_DOCS=0 \
        -DENABLE_TESTS=0 \
        -DCONFIG_ACCOUNTING=1 \
        -DCONFIG_INSPECTION=0 \
        -DCONFIG_MULTITHREAD=0 \
        -DCONFIG_RUNTIME_CPU_DETECT=0 \
        -DCONFIG_WEBM_IO=0 \
        -DBUILD_SHARED_LIBS=1 \
        -DCMAKE_BUILD_TYPE=Release

    emmake make -j${CORES}

    cd ..
fi

CONFIGURE_ARGS="-DENABLE_MULTITHREADING_SUPPORT=OFF -DWITH_GDK_PIXBUF=OFF -DWITH_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON -DENABLE_PLUGIN_LOADING=OFF"
#export PKG_CONFIG_PATH="${DIR}/libde265-${LIBDE265_VERSION}"

emcmake cmake $CONFIGURE_ARGS \
    -DCMAKE_EXE_LINKER_FLAGS="-lde265 -laom" \
    -DLIBDE265_INCLUDE_DIR="${DIR}/libde265-${LIBDE265_VERSION}" \
    -DLIBDE265_LIBRARY="-L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs" \
    -DAOM_INCLUDE_DIR="${DIR}/aom-${AOM_VERSION}/aom-source" \
    -DAOM_LIBRARY="-L${DIR}/aom-${AOM_VERSION}"
emmake make -j${CORES}

export TOTAL_MEMORY=16777216

LIBHEIFA="libheif/libheif.a"
EXPORTED_FUNCTIONS=$($EMSDK/upstream/bin/llvm-nm $LIBHEIFA --format=just-symbols | grep "^heif_\|^de265_\|^aom_" | grep "[^:]$" | sed 's/^/_/' | paste -sd "," -)

echo "Running Emscripten..."
emcc "$LIBHEIFA" \
    -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS,_free,_malloc,_memcpy" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s WASM=1 \
    -s STANDALONE_WASM=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s LLD_REPORT_UNDEFINED \
    -O3 \
    -std=c++11 \
    -L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs \
    -L${DIR}/aom-${AOM_VERSION} \
    -lde265 \
    -laom \
    -o libheif.wasm \
    --no-entry
