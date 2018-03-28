#!/bin/sh
set -e

VERSION=$1
TARGET=$2

if [ -z "$VERSION" ] || [ -z "$TARGET" ]; then
    echo "USAGE: $0 <sdk-version> <target-folder>"
    exit 1
fi

LIBSTDC_BASE=http://de.archive.ubuntu.com/ubuntu/pool/main/g/gcc-5
EMSDK_DOWNLOAD=https://s3.amazonaws.com/mozilla-games/emscripten/releases/emsdk-portable.tar.gz

CODENAME=$(/usr/bin/lsb_release --codename --short)
if [ "$CODENAME" = "trusty" ] && [ -e "/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.21" ]; then
    CONTENTS=$(curl --location $LIBSTDC_BASE)
    LIBSTDC_VERSION=$(echo $CONTENTS | sed 's|.*libstdc++6_\([^_]*\)_amd64\.deb.*|\1|g')
    TMPDIR=$(mktemp --directory)
    echo "Installing libstdc++6 $LIBSTDC_VERSION to fix Emscripten ..."
    echo "Extracting in $TMPDIR ..."
    curl "${LIBSTDC_BASE}/libstdc++6_${LIBSTDC_VERSION}_amd64.deb" > "$TMPDIR/libstdc++6_${LIBSTDC_VERSION}_amd64.deb"
    dpkg -x "$TMPDIR/libstdc++6_${LIBSTDC_VERSION}_amd64.deb" "$TMPDIR"
    sudo mv "$TMPDIR/usr/lib/x86_64-linux-gnu/"libstdc++* /usr/lib/x86_64-linux-gnu
    rm -rf "$TMPDIR"
fi

cd "$TARGET"
if [ ! -e emsdk-portable.tar.gz ]; then
    echo "Downloading SDK base system ..."
    curl "$EMSDK_DOWNLOAD" > emsdk-portable.tar.gz
    tar xf emsdk-portable.tar.gz
fi

cd emsdk-portable
echo "Updating SDK ..."
./emsdk update

echo "Installing SDK version ${VERSION} ..."
./emsdk install sdk-${VERSION}-64bit

echo "Activating SDK version ${VERSION} ..."
./emsdk activate sdk-${VERSION}-64bit
