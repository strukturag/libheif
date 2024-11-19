: # This install script was originally taken from libavif but might have been modified.

: # If you want to enable the DAV1D decoder, please check that the WITH_DAV1D CMake variable is set correctly.
: # You will also have to set the PKG_CONFIG_PATH to "third-party/dav1d/dist/lib/x86_64-linux-gnu/pkgconfig" so that the local DAV1D library is found.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # meson and ninja must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone -b 1.5.0 --depth 1 https://code.videolan.org/videolan/dav1d.git

cd dav1d

: # macOS might require: -Dc_args=-fno-stack-check
: # Build with asan: -Db_sanitize=address
: # Build with ubsan: -Db_sanitize=undefined
meson build --default-library=static --buildtype release --prefix "$(pwd)/dist" $@
ninja -C build
ninja -C build install
cd ..

echo ""
echo "----- NOTE ----"
echo "Please add the path to the pkg-config file to your PKG_CONFIG_PATH. For"
echo "example, on Linux x86_64, run:"
echo "export PKG_CONFIG_PATH=\$PKG_CONFIG_PATH:$(pwd)/dav1d/dist/lib/x86_64-linux-gnu/pkgconfig"
