: # This script downloads and builds the HEVC reference software HM, which libheif can use
: # as an HEVC encoder for the bit depths that x265 does not support (9, 11 and 13 to 15 bits).
: # The HM encoder plugin of libheif is EXPERIMENTAL.

: # If you want to enable the HM encoder, set the CMake variable WITH_HM to ON.
: # libheif looks for HM in "third-party/HM". For a different location, set HM_ROOT.

: # HM is built with support for high bit depths (HIGH_BITDEPTH), and only with it:
: # this changes the data types in the HM headers, and the libheif plugin expects this variant.

: # The header "libheif/plugins/hm_as_library.h" is included into every HM source file. It keeps HM from
: # printing to stdout and from exiting the process. See the comments in that file.

: # cmake and git must be in your PATH.

git clone -b HM-18.0 --depth 1 https://vcgit.hhi.fraunhofer.de/jvet/HM.git

cd HM

: # HM treats compiler warnings as errors, which breaks the build with compilers that are
: # newer than the HM version. There is no option to disable this.
sed -i.orig "s/ warnings-as-errors//" CMakeLists.txt

: # The symbols of HM are hidden, so that they cannot collide with those of another HM version
: # in the same process.

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHIGH_BITDEPTH=ON -DCMAKE_CXX_FLAGS="-include $(pwd)/../../libheif/plugins/hm_as_library.h -fvisibility=hidden -fvisibility-inlines-hidden" $@
cmake --build build --target TLibEncoder --target Utilities --parallel
cd ..

echo ""
echo "----- NOTE ----"
echo "Configure libheif with -DWITH_HM=ON to enable the HM encoder."
