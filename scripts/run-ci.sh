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

if [ ! -z "$CHECK_LICENSES" ]; then
    echo "Checking licenses ..."
    ./scripts/check-licenses.sh
fi

if [ ! -z "$CPPLINT" ]; then
    echo "Running cpplint ..."
    find -name "*.cc" -o -name "*.h" | sort | xargs ./scripts/cpplint.py
    exit 0
fi

if [ -z "$EMSCRIPTEN_VERSION" ] && [ -z "$CHECK_LICENSES" ] && [ -z "$TARBALL" ]; then
    echo "Building libheif ..."
    make
    echo "Dumping information of sample file ..."
    ./examples/heif-info --dump-boxes examples/example.heic
    if [ ! -z "$WITH_GRAPHICS" ] && [ ! -z "$WITH_LIBDE265" ]; then
        echo "Converting sample file to JPEG ..."
        ./examples/heif-convert examples/example.heic example.jpg
        [ -s "example.jpg" ] || exit 1
        echo "Converting sample file to PNG ..."
        ./examples/heif-convert examples/example.heic example.png
        [ -s "example.png" ] || exit 1
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
