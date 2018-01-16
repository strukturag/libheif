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

if [ -z "$CHECK_LICENSES" ] && [ -z "$CPPLINT" ]; then
    ./autogen.sh
    ./configure
fi
