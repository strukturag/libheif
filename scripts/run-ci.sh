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

BUILD_ROOT=$TRAVIS_BUILD_DIR

if [ ! -z "$CHECK_LICENSES" ]; then
    echo "Checking licenses ..."
    ./scripts/check-licenses.sh
fi

if [ ! -z "$CPPLINT" ]; then
    echo "Running cpplint ..."
    find -name "*.cc" -o -name "*.h" | sort | xargs ./scripts/cpplint.py
    ./scripts/check-emscripten-enums.sh
    ./scripts/check-go-enums.sh

    echo "Running gofmt ..."
    ./scripts/check-gofmt.sh
    exit 0
fi

BIN_SUFFIX=
BIN_WRAPPER=
if [ ! -z "$MINGW32" ]; then
    # Make sure the correct compiler will be used.
    unset CC
    unset CXX
    BIN_SUFFIX=.exe
    BIN_WRAPPER=wine
    export WINEPATH="/usr/lib/gcc/i686-w64-mingw32/4.8/;/usr/i686-w64-mingw32/lib"
elif [ ! -z "$MINGW64" ]; then
    # Make sure the correct compiler will be used.
    unset CC
    unset CXX
    BIN_SUFFIX=.exe
    BIN_WRAPPER=wine64
    export WINEPATH="/usr/lib/gcc/x86_64-w64-mingw32/4.8/;/usr/x86_64-w64-mingw32/lib"
elif [ ! -z "$FUZZER" ]; then
    export CC="$BUILD_ROOT/clang/bin/clang"
    export CXX="$BUILD_ROOT/clang/bin/clang++"
fi

if [ ! -z "$CMAKE" ]; then
    echo "Preparing cmake build files ..."
    cmake .
fi

if [ -z "$EMSCRIPTEN_VERSION" ] && [ -z "$CHECK_LICENSES" ] && [ -z "$TARBALL" ]; then
    echo "Building libheif ..."
    make
    echo "Dumping information of sample file ..."
    ${BIN_WRAPPER} ./examples/heif-info${BIN_SUFFIX} --dump-boxes examples/example.heic
    if [ ! -z "$WITH_GRAPHICS" ] && [ ! -z "$WITH_LIBDE265" ]; then
        echo "Converting sample file to JPEG ..."
        ${BIN_WRAPPER} ./examples/heif-convert${BIN_SUFFIX} examples/example.heic example.jpg
        echo "Checking first generated file ..."
        [ -s "example-1.jpg" ] || exit 1
        echo "Checking second generated file ..."
        [ -s "example-2.jpg" ] || exit 1
        echo "Converting sample file to PNG ..."
        ${BIN_WRAPPER} ./examples/heif-convert${BIN_SUFFIX} examples/example.heic example.png
        echo "Checking first generated file ..."
        [ -s "example-1.png" ] || exit 1
        echo "Checking second generated file ..."
        [ -s "example-2.png" ] || exit 1
    fi
    if [ ! -z "$GO" ]; then
        echo "Installing library ..."
        make install

        echo "Running golang example ..."
        export GOPATH="$BUILD_ROOT/gopath"
        export PKG_CONFIG_PATH="$BUILD_ROOT/dist/lib/pkgconfig"
        export LD_LIBRARY_PATH="$BUILD_ROOT/dist/lib"
        mkdir -p "$GOPATH/src/github.com/strukturag"
        ln -s "$BUILD_ROOT" "$GOPATH/src/github.com/strukturag/libheif"
        go run examples/heif-test.go examples/example.heic
        echo "Checking first generated file ..."
        [ -s "examples/example_lowlevel.png" ] || exit 1
        echo "Checking second generated file ..."
        [ -s "examples/example_highlevel.png" ] || exit 1
    fi
fi

if [ ! -z "$EMSCRIPTEN_VERSION" ]; then
    echo "Building with emscripten $EMSCRIPTEN_VERSION ..."
    source ./emscripten/emsdk-portable/emsdk_env.sh && ./build-emscripten.sh
    source ./emscripten/emsdk-portable/emsdk_env.sh && node scripts/test-javascript.js
fi

if [ ! -z "$TARBALL" ]; then
    VERSION=$(grep AC_INIT configure.ac | sed -r 's/^[^0-9]*([0-9]+\.[0-9]+\.[0-9]+).*/\1/g')
    echo "Creating tarball for version $VERSION ..."
    make dist

    echo "Building from tarball ..."
    tar xf libheif-$VERSION.tar*
    pushd libheif-$VERSION
    ./configure
    make
    popd
fi

if [ ! -z "$FUZZER" ] && [ "$TRAVIS_OS_NAME" = "linux" ]; then
    export ASAN_SYMBOLIZER="$BUILD_ROOT/clang/bin/llvm-symbolizer"
    ./libheif/file-fuzzer ./fuzzing/corpus/*

    echo "Running encoder fuzzer ..."
    ./libheif/encoder-fuzzer -max_total_time=120
    echo "Running file fuzzer ..."
    ./libheif/file-fuzzer -dict=./fuzzing/dictionary.txt -max_total_time=120
fi
