: # This install script was originally taken from libavif but might have been modified.

: # If you want to enable the libsharpyuv decoder, please check that the WITH_LIBSHARPYUV CMake variable is set correctly.
: # You will also have to set the PKG_CONFIG_PATH to "third-party/libwebp/build/dist/lib/pkgconfig" so that the local libsharpyuv library is found.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # meson and ninja must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
git clone --single-branch https://chromium.googlesource.com/webm/libwebp

cd libwebp
mkdir build
cd build
cmake -G Ninja -DCMAKE_INSTALL_PREFIX="$(pwd)/dist" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release ..
ninja sharpyuv
ninja install
cd ../..
