: # This install script was originally taken from libavif but might have been modified.

: # If you want to enable the RAV1E encoder, please check that the WITH_RAV1E CMake variable is set correctly.
: # You will also have to set the PKG_CONFIG_PATH to "third-party/rav1e/dist/lib/x86_64-linux-gnu/pkgconfig" (depending on your architecture) so that the local RAV1E library is found.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cargo must be in your PATH. (use rustup or brew to install)

: # If you're running this on Windows targeting Rust's windows-msvc, be sure you've already run this (from your VC2017 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars64.bat"
: #
: # Also, the error that "The target windows-msvc is not supported yet" can safely be ignored provided that rav1e/target/release
: # contains rav1e.h and rav1e.lib.

git clone -b v0.7.1 --depth 1 https://github.com/xiph/rav1e.git

cd rav1e
cargo install cargo-c
cargo cinstall --crt-static --release --prefix="$(pwd)/dist" --library-type=staticlib
cd ..
