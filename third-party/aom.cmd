: # This install script was originally taken from libavif but might have been modified.

: # If you want to enable the AOM encoder, please check that the WITH_AOM CMake variable is set correctly.
: # You will also have to set the PKG_CONFIG_PATH to "third-party/aom/dist/lib/pkgconfig" so that the local AOM library is found.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake and ninja must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone -b v3.12.1 --depth 1 https://aomedia.googlesource.com/aom

cd aom

cmake -S . -B build.libavif -G Ninja -DCMAKE_INSTALL_PREFIX="$(pwd)/dist" -DCMAKE_BUILD_TYPE=Release -DENABLE_DOCS=0 -DENABLE_EXAMPLES=0 -DENABLE_TESTDATA=0 -DENABLE_TESTS=0 -DENABLE_TOOLS=0
ninja -C build.libavif
ninja -C build.libavif install
cd ..

echo ""
echo "----- NOTE ----"
echo "Please add the path to the pkg-config file to your PKG_CONFIG_PATH, like this:"
echo "export PKG_CONFIG_PATH=\$PKG_CONFIG_PATH:$(pwd)/aom/dist/lib/pkgconfig"
