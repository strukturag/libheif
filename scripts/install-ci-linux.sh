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

INSTALL_PACKAGES=
REMOVE_PACKAGES=
BUILD_ROOT=$TRAVIS_BUILD_DIR
UPDATE_APT=

if [ "$WITH_LIBDE265" = "1" ]; then
    echo "Adding PPA strukturag/libde265 ..."
    sudo add-apt-repository -y ppa:strukturag/libde265
    UPDATE_APT=1
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        libde265-dev \
        "
fi

if [ "$WITH_LIBDE265" = "2" ]; then
    echo "Installing libde265 from frame-parallel branch ..."
    git clone --depth 1 -b frame-parallel https://github.com/strukturag/libde265.git
    pushd libde265
    ./autogen.sh
    ./configure \
        --prefix=$BUILD_ROOT/libde265/dist \
        --disable-dec265 \
        --disable-sherlock265 \
        --disable-hdrcopy \
        --disable-enc265 \
        --disable-acceleration_speed
    make && make install
    popd
fi

if [ "$WITH_X265" = "1" ]; then
    echo "Adding PPA strukturag/libheif ..."
    sudo add-apt-repository -y ppa:strukturag/libheif
    UPDATE_APT=1
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        libx265-dev \
        "
fi

if [ ! -z "$CHECK_LICENSES" ]; then
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        devscripts \
        "
fi

if [ -z "$WITH_GRAPHICS" ] && [ -z "$CHECK_LICENSES" ] && [ -z "$CPPLINT" ]; then
    REMOVE_PACKAGES="$REMOVE_PACKAGES \
        libjpeg.*-dev \
        libpng.*-dev \
        "
fi

if [ ! -z "$WITH_GRAPHICS" ]; then
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        libgdk-pixbuf2.0-dev \
        libjpeg-dev \
        libpng-dev \
        "
fi

if [ ! -z "$MINGW32" ]; then
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        binutils-mingw-w64-i686 \
        g++-mingw-w64-i686 \
        gcc-mingw-w64-i686 \
        mingw-w64-i686-dev \
        wine \
        "
elif [ ! -z "$MINGW64" ]; then
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        binutils-mingw-w64-x86-64 \
        g++-mingw-w64-x86-64 \
        gcc-mingw-w64-x86-64 \
        mingw-w64-x86-64-dev \
        wine \
        "
fi

if [ ! -z "$GO" ]; then
    INSTALL_PACKAGES="$INSTALL_PACKAGES \
        golang \
        "
fi

if [ ! -z "$UPDATE_APT" ]; then
    echo "Updating package lists ..."
    sudo apt-get update
fi

if [ ! -z "$INSTALL_PACKAGES" ]; then
    echo "Installing packages $INSTALL_PACKAGES ..."
    sudo apt-get install $INSTALL_PACKAGES
fi

if [ ! -z "$REMOVE_PACKAGES" ]; then
    echo "Removing packages $REMOVE_PACKAGES ..."
    sudo apt-get remove $REMOVE_PACKAGES
fi

if [ ! -z "$EMSCRIPTEN_VERSION" ]; then
    echo "Installing emscripten $EMSCRIPTEN_VERSION to $BUILD_ROOT/emscripten ..."
    mkdir -p $BUILD_ROOT/emscripten
    ./scripts/install-emscripten.sh $EMSCRIPTEN_VERSION $BUILD_ROOT/emscripten
fi

if [ ! -z "$FUZZER" ]; then
    ./scripts/install-clang.sh "$BUILD_ROOT/clang"
fi

if [ "$TRAVIS_BRANCH" = "coverity" ]; then
    echo "Installing coverity build tool ..."
    echo -n | openssl s_client -connect scan.coverity.com:443 | sed -ne '/-BEGIN CERTIFICATE-/,/-END CERTIFICATE-/p' | sudo tee -a /etc/ssl/certs/ca-certificates.crt
fi
