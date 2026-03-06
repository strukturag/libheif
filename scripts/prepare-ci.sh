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

# This was used with autotools, but not work cmake
#
#CONFIGURE_HOST=
#if [ "$MINGW" == "32" ]; then
#    CONFIGURE_HOST=i686-w64-mingw32
#elif [ "$MINGW" == "64" ]; then
#    CONFIGURE_HOST=x86_64-w64-mingw32
#fi
