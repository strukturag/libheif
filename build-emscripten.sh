#!/bin/bash
set -e
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

CORES="${CORES:-`nproc --all`}"
ENABLE_LIBDE265="${ENABLE_LIBDE265:-1}"
LIBDE265_VERSION="${LIBDE265_VERSION:-1.0.12}"
ENABLE_AOM="${ENABLE_AOM:-0}"
AOM_VERSION="${AOM_VERSION:-3.6.1}"
STANDALONE="${STANDALONE:0}"
DEBUG="${DEBUG:0}"

echo "Build using ${CORES} CPU cores"

LIBRARY_LINKER_FLAGS=""
LIBRARY_INCLUDE_FLAGS=""

CONFIGURE_ARGS_LIBDE265=""
if [ "$ENABLE_LIBDE265" = "1" ]; then
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
    CONFIGURE_ARGS_LIBDE265="-DLIBDE265_INCLUDE_DIR=${DIR}/libde265-${LIBDE265_VERSION} -DLIBDE265_LIBRARY=-L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs"
    LIBRARY_LINKER_FLAGS="$LIBRARY_LINKER_FLAGS -lde265"
    LIBRARY_INCLUDE_FLAGS="$LIBRARY_INCLUDE_FLAGS -L${DIR}/libde265-${LIBDE265_VERSION}/libde265/.libs"
fi

CONFIGURE_ARGS_AOM=""
if [ "$ENABLE_AOM" = "1" ]; then
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
            -DENABLE_EXAMPLES=0 \
            -DENABLE_TESTDATA=0 \
            -DENABLE_TOOLS=0 \
            -DCONFIG_MULTITHREAD=0 \
            -DCONFIG_RUNTIME_CPU_DETECT=0 \
            -DBUILD_SHARED_LIBS=1 \
            -DCMAKE_BUILD_TYPE=Release

        emmake make -j${CORES}

        cd ..
    fi

    CONFIGURE_ARGS_AOM="-DAOM_INCLUDE_DIR=${DIR}/aom-${AOM_VERSION}/aom-source -DAOM_LIBRARY=-L${DIR}/aom-${AOM_VERSION}"
    LIBRARY_LINKER_FLAGS="$LIBRARY_LINKER_FLAGS -laom"
    LIBRARY_INCLUDE_FLAGS="$LIBRARY_INCLUDE_FLAGS -L${DIR}/aom-${AOM_VERSION}"
fi

EXTRA_EXE_LINKER_FLAGS="-lembind"
EXTRA_COMPILER_FLAGS=""
if [ "$STANDALONE" = "1" ]; then
    EXTRA_EXE_LINKER_FLAGS=""
    EXTRA_COMPILER_FLAGS="-D__EMSCRIPTEN_STANDALONE_WASM__=1"
fi

CONFIGURE_ARGS="-DENABLE_MULTITHREADING_SUPPORT=OFF -DWITH_GDK_PIXBUF=OFF -DWITH_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON -DENABLE_PLUGIN_LOADING=OFF"
emcmake cmake $CONFIGURE_ARGS \
    -DCMAKE_C_FLAGS="${EXTRA_COMPILER_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${EXTRA_COMPILER_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${LIBRARY_LINKER_FLAGS} ${EXTRA_EXE_LINKER_FLAGS}" \
    $CONFIGURE_ARGS_LIBDE265 \
    $CONFIGURE_ARGS_AOM

emmake make -j${CORES}

LIBHEIFA="libheif/libheif.a"
EXPORTED_FUNCTIONS=$($EMSDK/upstream/bin/llvm-nm $LIBHEIFA --format=just-symbols | grep "^heif_\|^de265_\|^aom_" | grep "[^:]$" | sed 's/^/_/' | paste -sd "," -)

echo "Running Emscripten..."

BUILD_FLAGS="-lembind -o libheif.js --pre-js pre.js --post-js post.js"
RELEASE_BUILD_FLAGS="-O3"

if [ "$STANDALONE" = "1" ]; then
    echo "Building in standalone (non-web) build mode"
    BUILD_FLAGS="-s STANDALONE_WASM=1 -s WASM=1 -o libheif.wasm --no-entry"
fi

if [ "$DEBUG" = "1" ]; then
    echo "Building in debug mode"
    RELEASE_BUILD_FLAGS="--profile -g"
fi

emcc "$LIBHEIFA" \
    -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS,_free,_malloc,_memcpy" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
    -s LLD_REPORT_UNDEFINED \
    -std=c++11 \
    $LIBRARY_INCLUDE_FLAGS \
    $LIBRARY_LINKER_FLAGS \
    $BUILD_FLAGS \
    $RELEASE_BUILD_FLAGS
