#!/bin/bash
#
# HEIF codec.
# Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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


# This script builds binaries for all Android architectures. They are placed in the directory 'out'.
# The script should be run in the root directory of the libheif sources.

# The configuration below builds libheif with heic decoding only.

# Set these variables to suit your needs
NDK_PATH= ... # for example .../android-sdk/sdk/ndk/25.1.8937393
TOOLCHAIN=clang
ANDROID_VERSION=24 # the minimum version of Android to support

function build {
    mkdir -p build/$1
    cd build/$1
    cmake -G"Unix Makefiles" \
	  -DCMAKE_ASM_FLAGS="--target=arm-linux-androideabi${ANDROID_VERSION}" \
	  -DCMAKE_TOOLCHAIN_FILE=${NDK_PATH}/build/cmake/android.toolchain.cmake \
	  -DANDROID_ABI=$1 \
	  -DANDROID_ARM_MODE=arm \
	  -DANDROID_PLATFORM=android-${ANDROID_VERSION} \
	  -DANDROID_TOOLCHAIN=${TOOLCHAIN} \
    	  -DENABLE_PLUGIN_LOADING=OFF \
	  -DBUILD_TESTING=OFF \
	  -DWITH_AOM_DECODER=OFF \
	  -DWITH_AOM_DECODER_PLUGIN=OFF \
	  -DWITH_AOM_ENCODER=OFF \
	  -DWITH_DAV1D=OFF \
	  -DWITH_DEFLATE_HEADER_COMPRESSION=OFF \
	  -DWITH_EXAMPLES=OFF \
	  -DWITH_GDK_PIXBUF=OFF \
	  -DWITH_LIBDE265=ON \
	  -DWITH_LIBDE265_PLUGIN=OFF \
	  -DWITH_RAV1E=OFF \
	  -DWITH_SvtEnc=OFF \
	  -DWITH_X265=OFF \
	  -DCMAKE_INSTALL_PREFIX=../../out/$1 \
	  -DCMAKE_BUILD_TYPE=Release \
	  -Dld-version-script=OFF \
	  ../..

    make VERBOSE=1
    make install
    cd ../..

    mkdir -p out/$1
}

rm -rf build out


build armeabi-v7a ON
build arm64-v8a ON
build x86 OFF
build x86_64 OFF

rm -rf build
rm -rf out/*/share
