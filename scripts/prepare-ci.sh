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

if [ "$WITH_LIBDE265" = "2" ]; then
    export PKG_CONFIG_PATH=$BUILD_ROOT/libde265/dist/lib/pkgconfig/
fi

CONFIGURE_HOST=
if [ ! -z "$MINGW32" ]; then
    CONFIGURE_HOST=i686-w64-mingw32
elif [ ! -z "$MINGW64" ]; then
    CONFIGURE_HOST=x86_64-w64-mingw32
fi

if [ -z "$CHECK_LICENSES" ] && [ -z "$CPPLINT" ] && [ -z "$CMAKE" ]; then
    ./autogen.sh
    CONFIGURE_ARGS=
    if [ -z "$CONFIGURE_HOST" ]; then
        if [ ! -z "$FUZZER" ]; then
            export CC="$BUILD_ROOT/clang/bin/clang"
            export CXX="$BUILD_ROOT/clang/bin/clang++"
            FUZZER_FLAGS="-fsanitize=fuzzer-no-link,address,shift,integer -fno-sanitize-recover=shift,integer"
            export CFLAGS="$CFLAGS $FUZZER_FLAGS"
            export CXXFLAGS="$CXXFLAGS $FUZZER_FLAGS"
            CONFIGURE_ARGS="$CONFIGURE_ARGS --enable-libfuzzer=-fsanitize=fuzzer"
        fi
    else
        # Make sure the correct compiler will be used.
        unset CC
        unset CXX
        CONFIGURE_ARGS="$CONFIGURE_ARGS --host=$CONFIGURE_HOST"
    fi
    if [ ! -z "$GO" ]; then
        CONFIGURE_ARGS="$CONFIGURE_ARGS --prefix=$BUILD_ROOT/dist --disable-gdk-pixbuf"
    else
        CONFIGURE_ARGS="$CONFIGURE_ARGS --disable-go"
    fi
    if [ ! -z "$TESTS" ]; then
        CONFIGURE_ARGS="$CONFIGURE_ARGS --enable-tests"
    fi
    ./configure $CONFIGURE_ARGS
fi
