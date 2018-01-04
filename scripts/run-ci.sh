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

if [ -z "$EMSCRIPTEN_VERSION" ] && [ -z "$CHECK_LICENSES" ]; then
    echo "Building libheif ..."
    make
fi

if [ ! -z "$EMSCRIPTEN_VERSION" ]; then
    echo "Building with emscripten $EMSCRIPTEN_VERSION ..."
    source ./emscripten/emsdk-portable/emsdk_env.sh && ./build-emscripten.sh
fi
