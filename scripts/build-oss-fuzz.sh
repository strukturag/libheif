#!/bin/bash -e
#
# HEIF codec.
# Copyright (c) 2026 struktur AG, Joachim Bauch <bauch@struktur.de>
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

if [ -z "$SRC" ]; then
	echo "Environment variable SRC is not defined."
	exit 1
elif [ -z "$OUT" ]; then
	echo "Environment variable OUT is not defined."
	exit 1
elif [ -z "$WORK" ]; then
	echo "Environment variable WORK is not defined."
	exit 1
fi

# Install build dependencies.

apt-get update

apt-get install -y \
		autoconf \
		automake \
		build-essential \
		cmake \
		libbrotli-dev \
		libtool \
		make \
		mercurial \
		pkg-config \
		yasm \
		zlib1g-dev

# Install and build codec dependencies.

git clone \
		--depth 1 \
		--branch master \
		https://github.com/strukturag/libde265.git \
		"$WORK/libde265"

git clone \
		https://bitbucket.org/multicoreware/x265_git/src/stable/ \
		"$WORK/x265"

git clone \
		--depth 1 \
		--branch master \
		https://aomedia.googlesource.com/aom \
		"$WORK/aom"

export DEPS_PATH="$SRC/deps"
mkdir -p "$DEPS_PATH"

cd "$WORK/x265/build/linux"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DENABLE_SHARED:bool=off \
	../../source
make clean
make -j"$(nproc)" x265-static
make install

cd "$WORK/libde265"
./autogen.sh
./configure \
	--prefix="$DEPS_PATH" \
	--disable-shared \
	--enable-static \
	--disable-dec265 \
	--disable-sherlock265 \
	--disable-hdrcopy \
	--disable-enc265 \
	--disable-acceleration_speed
make clean
make -j"$(nproc)"
make install

mkdir -p "$WORK/aom/build/linux"
cd "$WORK/aom/build/linux"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DENABLE_SHARED:bool=off -DCONFIG_PIC=1 \
	-DENABLE_EXAMPLES=0 -DENABLE_DOCS=0 -DENABLE_TESTS=0 \
	-DCONFIG_SIZE_LIMIT=1 \
	-DDECODE_HEIGHT_LIMIT=12288 -DDECODE_WIDTH_LIMIT=12288 \
	-DDO_RANGE_CHECK_CLAMP=1 \
	-DAOM_MAX_ALLOCABLE_MEMORY=536870912 \
	-DAOM_TARGET_CPU=generic \
	../../
make clean
make -j"$(nproc)"
make install

# Remove shared libraries to avoid accidental linking against them.
rm -f "$DEPS_PATH/lib"/*.so
rm -f "$DEPS_PATH/lib/"*.so.*

cd "$SRC/libheif"
mkdir build
cd build
cmake .. --preset=fuzzing \
	-DFUZZING_COMPILE_OPTIONS="" \
	-DFUZZING_LINKER_OPTIONS="$LIB_FUZZING_ENGINE" \
	-DFUZZING_C_COMPILER="$CC" -DFUZZING_CXX_COMPILER="$CXX" \
	-DWITH_DEFLATE_HEADER_COMPRESSION=OFF

make -j"$(nproc)"

#./autogen.sh
#PKG_CONFIG="pkg-config --static" PKG_CONFIG_PATH="$DEPS_PATH/lib/pkgconfig" ./configure \
#	--disable-shared \
#	--enable-static \
#	--disable-examples \
#	--disable-go \
#	--enable-libfuzzer="$LIB_FUZZING_ENGINE" \
#	CPPFLAGS="-I$DEPS_PATH/include"
#make clean
#make -j"$(nproc)""

cp fuzzing/*_fuzzer "$OUT"
cp ../fuzzing/data/dictionary.txt "$OUT/box-fuzzer.dict"
cp ../fuzzing/data/dictionary.txt "$OUT/file-fuzzer.dict"

zip -r "$OUT/file-fuzzer_seed_corpus.zip" ../fuzzing/data/corpus/*.heic
