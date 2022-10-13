: # This install script was originally taken from libavif but might have been modified.

: # cmake and ninja must be in your PATH for compiling.

: # If you want to enable the SVT-AV1 encoder, please check that the WITH_SvtEnc and WITH_SvtEnc_BUILTIN CMake variables are set correctly.
: # You will also have to set the PKG_CONFIG_PATH to "third-party/SVT-AV1/Build/linux/Release" so that the local SVT-AV1 library is found.

git clone -b v1.2.1 --depth 1 https://gitlab.com/AOMediaCodec/SVT-AV1.git

cd SVT-AV1
cd Build/linux

./build.sh release static no-dec no-apps
cd ../..
mkdir -p include/svt-av1
cp Source/API/*.h include/svt-av1

cd ..
