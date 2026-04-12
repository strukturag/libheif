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
		meson \
		nasm \
		ninja-build \
		pkg-config \
		yasm \
		zlib1g-dev

# Install and build codec dependencies.

git clone \
		--depth 1 \
		--branch main \
		https://github.com/libjpeg-turbo/libjpeg-turbo.git \
		"$WORK/libjpeg-turbo"

git clone \
		--depth 1 \
		--branch master \
		https://github.com/strukturag/libde265.git \
		"$WORK/libde265"

git clone \
		--depth 1 \
		https://bitbucket.org/multicoreware/x265_git/src/stable/ \
		"$WORK/x265"

git clone \
		--depth 1 \
		--branch master \
		https://aomedia.googlesource.com/aom \
		"$WORK/aom"

git clone \
		--depth 1 \
		--branch master \
		https://code.videolan.org/videolan/dav1d.git \
		"$WORK/dav1d"

git clone \
		--depth 1 \
		--single-branch \
		https://chromium.googlesource.com/webm/libwebp \
		"$WORK/libwebp"

git clone \
		--depth 1 \
		--branch master \
		https://github.com/fraunhoferhhi/vvdec.git \
		"$WORK/vvdec"

git clone \
		--depth 1 \
		--branch master \
		https://github.com/fraunhoferhhi/vvenc.git \
		"$WORK/vvenc"

git clone \
		--depth 1 \
		--branch master \
		https://code.videolan.org/videolan/x264.git \
		"$WORK/x264"

git clone \
		--depth 1 \
		--branch master \
		https://gitlab.com/AOMediaCodec/SVT-AV1.git \
		"$WORK/svt-av1"

git clone \
		--depth 1 \
		--branch master \
		https://github.com/cisco/openh264.git \
		"$WORK/openh264"

git clone \
		--depth 1 \
		--branch master \
		https://github.com/uclouvain/openjpeg.git \
		"$WORK/openjpeg"

git clone \
		--depth 1 \
		--branch master \
		https://github.com/aous72/OpenJPH.git \
		"$WORK/openjph"

export DEPS_PATH="$SRC/deps"
mkdir -p "$DEPS_PATH"


echo ""
echo "=== Building libjpeg-turbo ==="
mkdir -p "$WORK/libjpeg-turbo/build"
cd "$WORK/libjpeg-turbo/build"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DENABLE_SHARED=OFF \
	-DENABLE_STATIC=ON \
	-DWITH_TURBOJPEG=OFF \
	-DWITH_SIMD=OFF \
	..
make -j"$(nproc)"
make install

echo ""
echo "=== Building x265 ==="
if [ -d "$WORK/x265/.git" ]; then
	mv "$WORK/x265/.git" "$WORK/x265/.git-unused"
fi
cd "$WORK/x265/build/linux"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DENABLE_SHARED:bool=off \
	-DENABLE_ASSEMBLY:bool=off \
	-DENABLE_CLI:bool=off \
	-DX265_LATEST_TAG=TRUE \
	../../source
make clean
make -j"$(nproc)" x265-static
make install
if [ -d "$WORK/x265/.git-unused" ]; then
	mv "$WORK/x265/.git-unused" "$WORK/x265/.git"
fi

echo ""
echo "=== Building libde265 ==="
cd "$WORK/libde265"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DBUILD_SHARED_LIBS:bool=off \
	-DENABLE_DECODER:bool=off \
	-DENABLE_ENCODER:bool=off \
	-DENABLE_SDL:bool=off \
	-DENABLE_SHERLOCK265:bool=off \
	.
make clean
make -j"$(nproc)"
make install

echo ""
echo "=== Building aom ==="
mkdir -p "$WORK/aom/build/linux"
cd "$WORK/aom/build/linux"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DENABLE_SHARED:bool=off -DCONFIG_PIC=1 \
	-DENABLE_EXAMPLES:bool=off -DENABLE_DOCS:bool=off -DENABLE_TESTS:bool=off \
	-DENABLE_TESTDATA:bool=off -DENABLE_TOOLS:bool=off \
	-DCONFIG_SIZE_LIMIT=1 \
	-DDECODE_HEIGHT_LIMIT=12288 -DDECODE_WIDTH_LIMIT=12288 \
	-DDO_RANGE_CHECK_CLAMP=1 \
	-DAOM_MAX_ALLOCABLE_MEMORY=536870912 \
	-DAOM_TARGET_CPU=generic \
	../../
make clean
make -j"$(nproc)"
make install

echo ""
echo "=== Building dav1d ==="
cd "$WORK/dav1d"
meson build \
	--default-library=static \
	--buildtype release \
	--prefix "$DEPS_PATH" \
	-D enable_tools=false \
	-D enable_tests=false
ninja -C build
ninja -C build install

echo ""
echo "=== Building libwebp (for sharpyuv) ==="
mkdir -p "$WORK/libwebp/build"
cd "$WORK/libwebp/build"
cmake -G Ninja \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_BUILD_TYPE=Release \
	..
ninja sharpyuv
ninja install

echo ""
echo "=== Building vvdec ==="
cd "$WORK/vvdec"
cmake -B build/release-static \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS -fPIC" -DCMAKE_CXX_FLAGS="$CXXFLAGS -fPIC" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DBUILD_SHARED_LIBS=FALSE \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_VERBOSE_MAKEFILE:BOOL=TRUE \
	-DVVDEC_ENABLE_WERROR=OFF \
	-DVVDEC_LIBRARY_ONLY=ON \
	.
cmake --build build/release-static -j"$(nproc)"
cmake --build build/release-static --target install

echo ""
echo "=== Building vvenc ==="
cd "$WORK/vvenc"
cmake -B build/release-static \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS -fPIC" -DCMAKE_CXX_FLAGS="$CXXFLAGS -fPIC" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DBUILD_SHARED_LIBS=FALSE \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_VERBOSE_MAKEFILE:BOOL=TRUE \
	-DVVENC_ENABLE_WERROR=OFF \
	-DVVENC_LIBRARY_ONLY=ON \
	.
cmake --build build/release-static -j"$(nproc)"
cmake --build build/release-static --target install

echo ""
echo "=== Building x264 ==="
cd "$WORK/x264"
./configure \
	--prefix="$DEPS_PATH" \
	--enable-static \
	--disable-shared \
	--disable-asm \
	--disable-cli
make -j"$(nproc)"
make install

echo ""
echo "=== Building SVT-AV1 ==="
cd "$WORK/svt-av1/Build/linux"
./build.sh \
	release \
	static \
	no-apps \
	disable-lto \
	c-only \
	prefix="$DEPS_PATH" \
	install

echo ""
echo "=== Building openh264 ==="
cd "$WORK/openh264"
make -j"$(nproc)" BUILDTYPE=Debug libopenh264.a
make -j"$(nproc)" BUILDTYPE=Debug PREFIX="$DEPS_PATH" install-static

echo ""
echo "=== Building openjpeg ==="
cd "$WORK/openjpeg"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DBUILD_CODEC=OFF \
	-DBUILD_SHARED_LIBS=OFF \
	-DBUILD_STATIC_LIBS=ON \
	.
make -j"$(nproc)"
make install

echo ""
echo "=== Building OpenJPH ==="
cd "$WORK/openjph"
cmake -G "Unix Makefiles" \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DBUILD_SHARED_LIBS=OFF \
	-DBUILD_STATIC_LIBS=ON \
	-DOJPH_ENABLE_TIFF_SUPPORT=OFF \
	-DOJPH_BUILD_EXECUTABLES=OFF \
	.
make -j"$(nproc)"
make install

# Remove shared libraries to avoid accidental linking against them.
rm -f "$DEPS_PATH/lib"/*.so
rm -f "$DEPS_PATH/lib/"*.so.*
rm -f /usr/lib/*/libjpeg.so
rm -f /usr/lib/*/libjpeg.so.*

echo ""
echo "=== Building libheif ==="
cd "$SRC/libheif"
mkdir build
cd build
PKG_CONFIG="pkg-config --static" PKG_CONFIG_PATH="$DEPS_PATH/lib/pkgconfig:$DEPS_PATH/lib/x86_64-linux-gnu/pkgconfig" cmake --preset=fuzzing \
	-DFUZZING_COMPILE_OPTIONS="" \
	-DFUZZING_LINKER_OPTIONS="$LIB_FUZZING_ENGINE" \
	-DFUZZING_C_COMPILER="$CC" -DFUZZING_CXX_COMPILER="$CXX" \
	-DCMAKE_INSTALL_PREFIX="$DEPS_PATH" \
	-DWITH_UNCOMPRESSED_CODEC=ON \
	-DWITH_JPEG_DECODER=ON \
	-DWITH_JPEG_ENCODER=ON \
	-DWITH_DAV1D=ON \
	-DWITH_LIBSHARPYUV=ON \
	-DWITH_VVDEC=ON \
	-DWITH_VVENC=ON \
	-DWITH_X264=ON \
	-DWITH_SvtEnc=ON \
	-DWITH_OpenH264_DECODER=ON \
	-DWITH_OpenJPEG_ENCODER=ON \
	-DWITH_OpenJPEG_DECODER=ON \
	-DWITH_OPENJPH_ENCODER=ON \
	..

make -j"$(nproc)"

cp fuzzing/*_fuzzer "$OUT"
cp ../fuzzing/data/dictionary.txt "$OUT/box-fuzzer.dict"
cp ../fuzzing/data/dictionary.txt "$OUT/file-fuzzer.dict"

zip -r "$OUT/file-fuzzer_seed_corpus.zip" ../fuzzing/data/corpus/*.heic
