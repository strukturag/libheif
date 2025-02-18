#!/bin/bash
set -e
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [[ $# -ne 1 ]] ; then
    echo "Usage: $0 SRCDIR"
    echo
    echo "It is recommended to build in a separate directory."
    echo "Then specify this directory as an argument to this script."
    echo "Example:"
    echo "  mkdir buildjs"
    echo "  cd buildjs"
    echo "  USE_WASM=0 ../build-emscripten.sh .."
    echo
    echo "This should generate a libheif.js and (optionally, without the USE_WASM=0) a libheif.wasm"
    exit 5
fi

SRCDIR=$1

CORES="${CORES:-`nproc --all`}"
ENABLE_LIBDE265="${ENABLE_LIBDE265:-1}"
LIBDE265_VERSION="${LIBDE265_VERSION:-1.0.15}"
ENABLE_AOM="${ENABLE_AOM:-0}"
AOM_VERSION="${AOM_VERSION:-3.6.1}"
STANDALONE="${STANDALONE:-0}"
DEBUG="${DEBUG:-0}"
USE_ES6="${USE_ES6:-0}"
USE_WASM="${USE_WASM:-1}"
USE_TYPESCRIPT="${USE_TYPESCRIPT:-1}"
USE_UNSAFE_EVAL="${USE_UNSAFE_EVAL:-1}"

echo "Build using ${CORES} CPU cores"

LIBRARY_LINKER_FLAGS=""
LIBRARY_INCLUDE_FLAGS=""

CONFIGURE_ARGS_LIBDE265=""
if [ "$ENABLE_LIBDE265" = "1" ]; then
    [ -s "libde265-${LIBDE265_VERSION}.tar.gz" ] || curl \
        -L \
        -o libde265-${LIBDE265_VERSION}.tar.gz \
        https://github.com/strukturag/libde265/releases/download/v${LIBDE265_VERSION}/libde265-${LIBDE265_VERSION}.tar.gz
    if [ ! -s "libde265-${LIBDE265_VERSION}/libde265/.libs/libde265.a" ]; then
        tar xf libde265-${LIBDE265_VERSION}.tar.gz
        cd libde265-${LIBDE265_VERSION}
        [ -x configure ] || ./autogen.sh
        CXXFLAGS=-O3 emconfigure ./configure --enable-static --disable-shared --disable-sse --disable-dec265 --disable-sherlock265
        emmake make -j${CORES}
        cd ..
    fi
    LIBDE265_DIR="$(pwd)/libde265-${LIBDE265_VERSION}"
    CONFIGURE_ARGS_LIBDE265="-DLIBDE265_INCLUDE_DIR=${LIBDE265_DIR} -DLIBDE265_LIBRARY=-L${LIBDE265_DIR}/libde265/.libs"
    LIBRARY_LINKER_FLAGS="$LIBRARY_LINKER_FLAGS -L${LIBDE265_DIR}/libde265/.libs -lde265"
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
            -DBUILD_SHARED_LIBS=0 \
            -DCMAKE_BUILD_TYPE=Release

        emmake make -j${CORES}

        cd ..
    fi

    AOM_DIR="$(pwd)/aom-${AOM_VERSION}"
    CONFIGURE_ARGS_AOM="-DAOM_INCLUDE_DIR=${AOM_DIR}/aom-source -DAOM_LIBRARY=-L${AOM_DIR}"
    LIBRARY_LINKER_FLAGS="$LIBRARY_LINKER_FLAGS -L${AOM_DIR} -laom"
fi

EXTRA_EXE_LINKER_FLAGS="-lembind"
EXTRA_COMPILER_FLAGS=""
if [ "$STANDALONE" = "1" ]; then
    EXTRA_EXE_LINKER_FLAGS=""
    EXTRA_COMPILER_FLAGS="-D__EMSCRIPTEN_STANDALONE_WASM__=1"
fi

CONFIGURE_ARGS="-DENABLE_MULTITHREADING_SUPPORT=OFF -DWITH_GDK_PIXBUF=OFF -DWITH_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DENABLE_PLUGIN_LOADING=OFF"
emcmake cmake ${SRCDIR} $CONFIGURE_ARGS \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${EXTRA_COMPILER_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${EXTRA_COMPILER_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${LIBRARY_LINKER_FLAGS} ${EXTRA_EXE_LINKER_FLAGS}" \
    $CONFIGURE_ARGS_LIBDE265 \
    $CONFIGURE_ARGS_AOM

VERBOSE=1 emmake make -j${CORES}

LIBHEIFA="libheif/libheif.a"
EXPORTED_FUNCTIONS=$($EMSDK/upstream/bin/llvm-nm $LIBHEIFA --format=just-symbols | grep "^heif_\|^de265_\|^aom_" | grep "[^:]$" | sed 's/^/_/' | paste -sd "," -)

echo "Running Emscripten..."

BUILD_FLAGS="-lembind -o libheif.js --post-js ${SRCDIR}/post.js -sWASM=$USE_WASM -sDYNAMIC_EXECUTION=$USE_UNSAFE_EVAL"

if [ "$USE_TYPESCRIPT" = "1" ]; then
    BUILD_FLAGS="$BUILD_FLAGS --emit-tsd libheif.d.ts"
fi

RELEASE_BUILD_FLAGS="-O3"

if [ "$STANDALONE" = "1" ]; then
    # Note: this intentionally overwrites the BUILD_FLAGS set above
    echo "Building in standalone (non-web) build mode"
    BUILD_FLAGS="-sSTANDALONE_WASM -sWASM -o libheif.wasm --no-entry"
fi

if [ "$DEBUG" = "1" ]; then
    echo "Building in debug mode"
    RELEASE_BUILD_FLAGS="--profile -g"
fi

if [ "$USE_ES6" = "1" ]; then
    BUILD_FLAGS="$BUILD_FLAGS -sEXPORT_ES6"
fi

emcc -Wl,--whole-archive "$LIBHEIFA" -Wl,--no-whole-archive \
    -sEXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS,_free,_malloc,_memcpy" \
    -sMODULARIZE \
    -sEXPORT_NAME="libheif" \
    -sWASM_ASYNC_COMPILATION=0 \
    -sALLOW_MEMORY_GROWTH \
    -std=c++11 \
    $LIBRARY_INCLUDE_FLAGS \
    $LIBRARY_LINKER_FLAGS \
    $BUILD_FLAGS \
    $RELEASE_BUILD_FLAGS
