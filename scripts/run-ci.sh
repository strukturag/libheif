#!/bin/bash
set -e
#
# HEIF codec.
# Copyright (c) 2018 struktur AG, Joachim Bauch <bauch@struktur.de>
#
# This file is part of libheif.
#
# libheif is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# libheif is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with libheif.  If not, see <http://www.gnu.org/licenses/>.
#

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

BUILD_ROOT=$ROOT/..
CURRENT_OS=$TRAVIS_OS_NAME
if [ -z "$CURRENT_OS" ]; then
    if [ "$(uname)" != "Darwin" ]; then
        CURRENT_OS=linux
    else
        CURRENT_OS=osx
    fi
fi

# Don't run regular tests on Coverity scan builds.
if [ ! -z "${COVERITY_SCAN_BRANCH}" ]; then
    echo "Skipping tests on Coverity scan build ..."
    exit 0
fi

if [ ! -z "$CHECK_LICENSES" ]; then
    echo "Checking licenses ..."
    ./scripts/check-licenses.sh
fi

if [ ! -z "$CPPLINT" ]; then
    PYTHON=$(which python || true)
    if [ -z "$PYTHON" ]; then
        PYTHON=$(which python3 || true)
        if [ -z "$PYTHON" ]; then
            echo "Could not find valid Python interpreter to run cpplint."
            echo "Make sure you have either python or python3 in your PATH."
            exit 1
        fi
    fi
    echo "Running cpplint with $PYTHON ..."
    find -name "*.c" -o -name "*.cc" -o -name "*.h" | sort | xargs "$PYTHON" ./scripts/cpplint.py --extensions=c,cc,h
    ./scripts/check-emscripten-enums.sh
    ./scripts/check-go-enums.sh

    echo "Running gofmt ..."
    ./scripts/check-gofmt.sh
    exit 0
fi

BIN_SUFFIX=
BIN_WRAPPER=
if [ "$MINGW" == "32" ]; then
    # Make sure the correct compiler will be used.
    export CC=i686-w64-mingw32-gcc
    export CXX=i686-w64-mingw32-g++
    BIN_SUFFIX=.exe
    BIN_WRAPPER=/usr/lib/wine/wine
    MINGW_SYSROOT="/usr/i686-w64-mingw32"
    export WINEPATH="$(dirname $($CXX --print-file-name=libstdc++.a));$MINGW_SYSROOT/lib"
elif [ "$MINGW" == "64" ]; then
    # Make sure the correct compiler will be used.
    export CC=x86_64-w64-mingw32-gcc
    export CXX=x86_64-w64-mingw32-g++
    BIN_SUFFIX=.exe
    BIN_WRAPPER=/usr/lib/wine/wine64
    MINGW_SYSROOT="/usr/x86_64-w64-mingw32"
    export WINEPATH="$(dirname $($CXX --print-file-name=libstdc++.a));$MINGW_SYSROOT/lib"
fi

PKG_CONFIG_PATH=
if [ "$WITH_LIBDE265" = "2" ]; then
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$BUILD_ROOT/libde265/dist/lib/pkgconfig/"
fi

if [ "$WITH_RAV1E" = "1" ]; then
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$BUILD_ROOT/third-party/rav1e/dist/lib/pkgconfig/"
fi

if [ "$WITH_DAV1D" = "1" ]; then
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$BUILD_ROOT/third-party/dav1d/dist/lib/x86_64-linux-gnu/pkgconfig/"
fi
if [ ! -z "$PKG_CONFIG_PATH" ]; then
    export PKG_CONFIG_PATH="$PKG_CONFIG_PATH"
fi

WITH_AVIF_DECODER=
if [ ! -z "$WITH_AOM" ] || [ ! -z "$WITH_DAV1D" ]; then
    WITH_AVIF_DECODER=1
fi
WITH_HEIF_DECODER=
if [ ! -z "$WITH_LIBDE265" ] ; then
    WITH_HEIF_DECODER=1
fi
WITH_AVIF_ENCODER=
WITH_HEIF_ENCODER=
# Need decoded images before encoding.
if [ ! -z "$WITH_AVIF_DECODER" ]; then
    if [ ! -z "$WITH_RAV1E" ]; then
        WITH_AVIF_ENCODER=1
    fi
fi
if [ ! -z "$WITH_HEIF_DECODER" ]; then
    if [ ! -z "$WITH_X265" ] ; then
        WITH_HEIF_ENCODER=1
    fi
fi


echo "Preparing cmake build files ..."

if [ ! -z "$FUZZER" ]; then
    CMAKE_OPTIONS="--preset=fuzzing"
fi
if [ ! -z "$TESTS" ]; then
    CMAKE_OPTIONS="--preset=testing"
fi
if [ -z "$FUZZER" ] && [ -z "$TESTS" ]; then
    CMAKE_OPTIONS="--preset=release -DENABLE_PLUGIN_LOADING=OFF"
fi
	
if [ "$CURRENT_OS" = "osx" ] ; then
    # Make sure the homebrew installed libraries are used when building instead
    # of the libraries provided by Apple.
    CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_FIND_FRAMEWORK=LAST"
fi
if [ -n "$MINGW" ]; then
    CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_FIND_ROOT_PATH=$MINGW_SYSROOT"
fi
if [ "$CLANG_TIDY" = "1" ]; then
    CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
fi

echo "install prefix: ${BUILD_ROOT}/dist"
mkdir ${BUILD_ROOT}/dist
CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_INSTALL_PREFIX=${BUILD_ROOT}/dist"

# turn on warnings-as-errors
CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_COMPILE_WARNING_AS_ERROR=1"

# compilation mode
CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_BUILD_TYPE=Release"


if [ ! -z "$FUZZER" ] && [ "$CURRENT_OS" = "linux" ]; then
    export ASAN_SYMBOLIZER="$BUILD_ROOT/clang/bin/llvm-symbolizer"
fi

if [ -z "$EMSCRIPTEN_VERSION" ] && [ -z "$CHECK_LICENSES" ] && [ -z "$TARBALL" ] ; then
    echo "Building libheif ..."
    cmake . $CMAKE_OPTIONS
    make -j $(nproc)
    if [ "$CURRENT_OS" = "linux" ] && [ -z "$MINGW" ] && [ -z "$FUZZER" ] && [ ! -z "$TESTS" ] ; then
        echo "Running tests ..."
        make test
    fi
    if [ -z "$FUZZER" ] ; then
	echo "List available encoders"
        ${BIN_WRAPPER} ./examples/heif-enc${BIN_SUFFIX} --list-encoders

	echo "List available decoders"
        ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} --list-decoders

        echo "Dumping information of sample file ..."
        #${BIN_WRAPPER} gdb -batch -ex "run" -ex "bt" --args ./examples/heif-info${BIN_SUFFIX} --dump-boxes examples/example.heic
        ${BIN_WRAPPER} ./examples/heif-info${BIN_SUFFIX} --dump-boxes examples/example.heic
        if [ ! -z "$WITH_GRAPHICS" ] && [ ! -z "$WITH_HEIF_DECODER" ]; then
            echo "Converting sample HEIF file to JPEG ..."
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} examples/example.heic example.jpg
            echo "Checking first generated file ..."
            [ -s "example-1.jpg" ] || exit 1
            echo "Checking second generated file ..."
            [ -s "example-2.jpg" ] || exit 1
            echo "Converting sample HEIF file to PNG ..."
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} examples/example.heic example.png
            echo "Checking first generated file ..."
            [ -s "example-1.png" ] || exit 1
            echo "Checking second generated file ..."
            [ -s "example-2.png" ] || exit 1
        fi
        if [ ! -z "$WITH_GRAPHICS" ] && [ ! -z "$WITH_AVIF_DECODER" ]; then
            echo "Converting sample AVIF file to JPEG ..."
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} examples/example.avif example.jpg
            echo "Checking generated file ..."
            [ -s "example.jpg" ] || exit 1
            echo "Converting sample AVIF file to PNG ..."
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} examples/example.avif example.png
            echo "Checking generated file ..."
            [ -s "example.png" ] || exit 1
        fi
        if [ ! -z "$WITH_GRAPHICS" ] && [ ! -z "$WITH_HEIF_ENCODER" ]; then
            echo "Converting single JPEG file to heif ..."
            ${BIN_WRAPPER} ./examples/heif-enc${BIN_SUFFIX} -o output-single.heic --verbose --verbose --verbose --thumb 320x240 example-1.jpg
            echo "Checking generated file ..."
            [ -s "output-single.heic" ] || exit 1
            echo "Converting back generated heif to JPEG ..."
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} output-single.heic output-single.jpg
            echo "Checking generated file ..."
            [ -s "output-single.jpg" ] || exit 1
            echo "Converting multiple JPEG files to heif ..."
            ${BIN_WRAPPER} ./examples/heif-enc${BIN_SUFFIX} -o output-multi.heic --verbose --verbose --verbose --thumb 320x240 example-1.jpg example-2.jpg
            echo "Checking generated file ..."
            [ -s "output-multi.heic" ] || exit 1
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} output-multi.heic output-multi.jpg
            echo "Checking first generated file ..."
            [ -s "output-multi-1.jpg" ] || exit 1
            echo "Checking second generated file ..."
            [ -s "output-multi-2.jpg" ] || exit 1
        fi
        if [ ! -z "$WITH_GRAPHICS" ] && [ ! -z "$WITH_AVIF_ENCODER" ]; then
            echo "Converting JPEG file to AVIF ..."
            ${BIN_WRAPPER} ./examples/heif-enc${BIN_SUFFIX} -o output-jpeg.avif --verbose --verbose --verbose -A --thumb 320x240 example.jpg
            echo "Checking generated file ..."
            [ -s "output-jpeg.avif" ] || exit 1
            echo "Converting back generated AVIF to JPEG ..."
            ${BIN_WRAPPER} ./examples/heif-dec${BIN_SUFFIX} output-jpeg.avif output-jpeg.jpg
            echo "Checking generated file ..."
            [ -s "output-jpeg.jpg" ] || exit 1
        fi
        if [ ! -z "$GO" ]; then
            echo "Installing library ..."
            make -j $(nproc) install

            echo "Running golang example ..."
            export GOPATH="$BUILD_ROOT/gopath"
            export PKG_CONFIG_PATH="$BUILD_ROOT/dist/lib/pkgconfig:$BUILD_ROOT/libde265/dist/lib/pkgconfig/"
            export LD_LIBRARY_PATH="$BUILD_ROOT/dist/lib:$BUILD_ROOT/libde265/dist/lib"
            mkdir -p "$GOPATH/src/github.com/strukturag"
            ln -s "$BUILD_ROOT" "$GOPATH/src/github.com/strukturag/libheif"
            go run examples/heif-test.go examples/example.heic
            echo "Checking first generated file ..."
            [ -s "examples/example_lowlevel.png" ] || exit 1
            echo "Checking second generated file ..."
            [ -s "examples/example_highlevel.png" ] || exit 1
            echo "Checking race tester ..."
            go run tests/test-race.go examples/example.heic
        fi
    fi
fi

if [ ! -z "$EMSCRIPTEN_VERSION" ]; then
    echo "Building with emscripten $EMSCRIPTEN_VERSION ..."
    source ./emscripten/emsdk/emsdk_env.sh && USE_WASM=0 ./build-emscripten.sh .
    source ./emscripten/emsdk/emsdk_env.sh && node scripts/test-javascript.js
fi

if [ ! -z "$TARBALL" ]; then
    VERSION=$(grep project CMakeLists.txt | sed -r 's/^[^0-9]*([0-9]+\.[0-9]+\.[0-9]+).*/\1/g')
    echo "Creating tarball for version $VERSION ..."
    mkdir build
    pushd build
    cmake .. --preset=release
    make package_source
    mv libheif-$VERSION.tar.gz ..
    popd

    echo "Building from tarball ..."
    tar xf libheif-$VERSION.tar*
    pushd libheif-$VERSION
    mkdir build
    pushd build
    cmake .. --preset=release
    make -j $(nproc)
    popd
fi

if [ ! -z "$FUZZER" ] && [ "$CURRENT_OS" = "linux" ]; then
    ./fuzzing/color_conversion_fuzzer ./fuzzing/data/corpus/*color-conversion-fuzzer*
    ./fuzzing/file_fuzzer ./fuzzing/data/corpus/*.heic

    echo "Running color conversion fuzzer ..."
    ./fuzzing/color_conversion_fuzzer -max_total_time=120

    # Do not run encoder_fuzzer because it will just find errors in x265...
    #echo "Running encoder fuzzer ..."
    #./fuzzing/encoder_fuzzer -max_total_time=120
    
    echo "Running file fuzzer ..."
    ./fuzzing/file_fuzzer -dict=./fuzzing/data/dictionary.txt -max_total_time=120
fi
