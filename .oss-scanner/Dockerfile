# Build of libheif for Anthropic's OSS Scanner (https://github.com/anthropics/oss-scanner).
#
# The scanner builds this file with the libheif checkout as the build context and then
# analyses the image offline, so everything the analysis needs is fetched here.
#
# The image holds three builds of the same checkout, all compiled with clang, with debug
# information and frame pointers:
#
#   /src/build          Debug, AddressSanitizer + UndefinedBehaviorSanitizer, all codecs,
#                       the example programs and the unit tests.
#                       Use this build to reproduce memory-safety findings.
#   /src/build-release  RelWithDebInfo (-O2, asserts off), no sanitizers.
#                       Use this build to measure time and memory. The sanitizer build
#                       is 20 to 100 times slower and must not be used for timing claims.
#   /src/build-fuzz     The libFuzzer harnesses from fuzzing/ with ASan and
#                       -fsanitize=integer, configured as the project fuzzes them.
#
# libde265 (HEVC decoder, same maintainer) is built from its git master instead of the
# distribution package, once plain and once with sanitizers, so that findings inside
# libde265 come with instrumented stack frames. The two installs are in /opt/libde265.
#
# /opt/pixbuf-loaders holds gdk-pixbuf loader caches that point at the gdk-pixbuf module of the
# sanitizer build and of the release build.
# /opt/heif-testcorpus holds the maintainer's HEIF test corpus (valid files: MPEG and Nokia
# conformance files plus own images), with a decode baseline of every file at
# /opt/heif-testcorpus/decode-baseline.txt and the script that produced it, heif-corpus-sweep.
#
# .oss-scanner/threat_model.md explains how to use all of this.

FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive \
    CC=clang \
    CXX=clang++

# Build tools (libclang-rt-dev holds clang's sanitizer and libFuzzer runtimes; GNU time is for
# measuring run time and peak memory), the codec libraries libheif can use, and gdk-pixbuf for the
# pixbuf loader (libgdk-pixbuf2.0-bin provides gdk-pixbuf-query-loaders). VVC (vvdec/vvenc) and uvg266 are
# not packaged in Debian and are left out. Package versions are not pinned on purpose:
# pinned Debian versions break on every point release.
# hadolint ignore=DL3008
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git build-essential clang llvm libclang-rt-dev cmake ninja-build pkg-config python3 gdb time \
      libaom-dev libdav1d-dev librav1e-dev libsvtav1enc-dev \
      libx265-dev libkvazaar-dev libx264-dev libopenh264-dev libavcodec-dev \
      libopenjp2-7-dev libopenjph-dev \
      libjpeg-dev libpng-dev libtiff-dev libsharpyuv-dev \
      libbrotli-dev zlib1g-dev \
      libgdk-pixbuf-2.0-dev libgdk-pixbuf2.0-bin \
    && rm -rf /var/lib/apt/lists/*

# Sanitizer flags shared by the libde265 and libheif sanitizer builds.
ENV SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"

# Runtime defaults for the sanitizer builds. Leak reports are off (the codec libraries leak,
# and leaks are not security findings here). Failed allocations return NULL instead of
# aborting, so that libheif's own out-of-memory handling is what gets tested.
ENV ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 \
    UBSAN_OPTIONS=print_stacktrace=1

# libde265 from git master: plain (for build-release) and sanitized (for build and build-fuzz).
RUN set -e; \
    git clone --depth 1 https://github.com/strukturag/libde265.git /opt/libde265/src; \
    for variant in plain asan; do \
      if [ "$variant" = asan ]; then flags="$SAN_FLAGS"; type=Debug; \
      else flags="-g -fno-omit-frame-pointer"; type=RelWithDebInfo; fi; \
      cmake -S /opt/libde265/src -B /opt/libde265/build-$variant -G Ninja \
        -DCMAKE_BUILD_TYPE=$type \
        -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
        -DCMAKE_INSTALL_PREFIX=/opt/libde265/$variant -DCMAKE_INSTALL_LIBDIR=lib \
        -DENABLE_SDL=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF; \
      cmake --build /opt/libde265/build-$variant -j"$(nproc)"; \
      cmake --install /opt/libde265/build-$variant; \
    done; \
    rm -rf /opt/libde265/build-plain /opt/libde265/build-asan

# Test corpus of valid files, https://github.com/farindk/heif-testcorpus (MIT). The third-party
# conformance files are downloaded and verified by its build script, not redistributed, so the
# corpus has to be built now, while the network is available. It is auxiliary: a download
# problem must not fail the scan.
RUN git clone --depth 1 https://github.com/farindk/heif-testcorpus.git /opt/heif-testcorpus \
 && (python3 /opt/heif-testcorpus/build-corpus.py || echo "WARNING: the test corpus did not build completely") \
 && rm -rf /opt/heif-testcorpus/.cache

# scanner contract: the checkout lives inside the image, at /src
COPY . /src
WORKDIR /src

# CMake options shared by all three libheif builds: every codec that is installed above,
# codecs built in (no plugin loading), full symbol visibility (needed by the unit tests),
# experimental API enabled (needed by some unit tests; see the threat model for how such
# findings are rated), and warnings never fatal so that a new compiler does not break the scan.
ENV LIBHEIF_CMAKE_OPTIONS="\
 -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF \
 -DENABLE_PLUGIN_LOADING=OFF \
 -DWITH_REDUCED_VISIBILITY=OFF \
 -DENABLE_EXPERIMENTAL_FEATURES=ON \
 -DWITH_EXAMPLES=ON \
 -DWITH_LIBDE265=ON -DWITH_X265=ON -DWITH_KVAZAAR=ON \
 -DWITH_AOM_DECODER=ON -DWITH_AOM_ENCODER=ON -DWITH_DAV1D=ON -DWITH_RAV1E=ON -DWITH_SvtEnc=ON \
 -DWITH_X264=ON -DWITH_OpenH264_DECODER=ON -DWITH_FFMPEG_DECODER=ON \
 -DWITH_JPEG_DECODER=ON -DWITH_JPEG_ENCODER=ON \
 -DWITH_OpenJPEG_DECODER=ON -DWITH_OpenJPEG_ENCODER=ON -DWITH_OPENJPH_ENCODER=ON \
 -DWITH_UNCOMPRESSED_CODEC=ON -DWITH_HEADER_COMPRESSION=ON -DWITH_LIBSHARPYUV=ON"

# 1. Sanitizer build with examples and unit tests.
RUN set -e; \
    export PKG_CONFIG_PATH=/opt/libde265/asan/lib/pkgconfig; \
    cmake -S /src -B /src/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="$SAN_FLAGS" -DCMAKE_CXX_FLAGS="$SAN_FLAGS" \
      -DCMAKE_PREFIX_PATH=/opt/libde265/asan -DCMAKE_BUILD_RPATH=/opt/libde265/asan/lib \
      -DBUILD_TESTING=ON \
      $LIBHEIF_CMAKE_OPTIONS; \
    cmake --build /src/build -j"$(nproc)"

# Run the unit tests so that a broken image is noticed, but do not let a failing test
# abort the build: the maintainer sees the log either way.
RUN ctest --test-dir /src/build --output-on-failure -j"$(nproc)" --timeout 900 || echo "WARNING: ctest reported failures"

# 2. Release build for timing and memory measurements.
RUN set -e; \
    export PKG_CONFIG_PATH=/opt/libde265/plain/lib/pkgconfig; \
    cmake -S /src -B /src/build-release -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS="-fno-omit-frame-pointer" -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
      -DCMAKE_PREFIX_PATH=/opt/libde265/plain -DCMAKE_BUILD_RPATH=/opt/libde265/plain/lib \
      -DBUILD_TESTING=OFF \
      $LIBHEIF_CMAKE_OPTIONS; \
    cmake --build /src/build-release -j"$(nproc)"

# 3. Fuzzer build. WITH_FUZZERS applies the project's fuzzing flags
#    (-fsanitize=fuzzer,address,shift,integer) to a static libheif.
RUN set -e; \
    export PKG_CONFIG_PATH=/opt/libde265/asan/lib/pkgconfig; \
    cmake -S /src -B /src/build-fuzz -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DWITH_FUZZERS=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
      -DCMAKE_PREFIX_PATH=/opt/libde265/asan -DCMAKE_BUILD_RPATH=/opt/libde265/asan/lib \
      $LIBHEIF_CMAKE_OPTIONS -DWITH_EXAMPLES=OFF; \
    cmake --build /src/build-fuzz -j"$(nproc)"

# Smoke test: every build decodes the sample files. A failure here fails the build on purpose.
RUN set -e; \
    /src/build/examples/heif-info /src/examples/example.heic >/dev/null; \
    /src/build/examples/heif-dec /src/examples/example.heic /tmp/smoke.png; \
    /src/build/examples/heif-dec /src/examples/example.avif /tmp/smoke.png; \
    /src/build-release/examples/heif-dec /src/examples/example.heic /tmp/smoke.png; \
    /src/build-release/examples/heif-dec /src/examples/example.avif /tmp/smoke.png; \
    /src/build-fuzz/fuzzing/file_fuzzer /src/fuzzing/data/corpus/avif32.heif >/dev/null 2>&1; \
    /src/build/examples/heif-thumbnailer -s 128 /src/examples/example.heic /tmp/smoke.png; \
    rm -f /tmp/smoke.png

# Loader caches for the gdk-pixbuf module of the release and the sanitizer build, so that
# GDK_PIXBUF_MODULE_FILE=/opt/pixbuf-loaders/<build>.cache makes any gdk-pixbuf program use it.
# gdk-pixbuf-query-loaders is a plain binary and cannot load the sanitized module, so that cache
# is derived from the release one. The release module is smoke-tested through the stock
# gdk-pixbuf-thumbnailer.
RUN set -e; \
    q=$(pkg-config --variable=gdk_pixbuf_query_loaders gdk-pixbuf-2.0); \
    [ -x "$q" ] || q=$(find /usr/lib -name gdk-pixbuf-query-loaders -type f -print -quit); \
    mkdir -p /opt/pixbuf-loaders; \
    "$q" /src/build-release/gdk-pixbuf/libpixbufloader-heif.so > /opt/pixbuf-loaders/build-release.cache; \
    sed 's#/src/build-release/gdk-pixbuf/#/src/build/gdk-pixbuf/#' /opt/pixbuf-loaders/build-release.cache > /opt/pixbuf-loaders/build.cache; \
    GDK_PIXBUF_MODULE_FILE=/opt/pixbuf-loaders/build-release.cache gdk-pixbuf-thumbnailer -s 128 /src/examples/example.heic /tmp/smoke.png; \
    rm -f /tmp/smoke.png

# heif-corpus-sweep decodes every corpus file with one build (default: the sanitizer build) and
# prints one line per file: "ok" (still image), "ok-seq" (image sequence, decoded with
# -S --ignore-editlist, so that a repeating edit list cannot make the sweep run for hours) or
# "FAIL FILE: reason". The baseline taken now is what a patched build has to be compared
# against: a file that goes from ok to FAIL means the patch breaks valid input.
# The single quotes below are intended: the script text must not expand at build time.
# hadolint ignore=SC2016
RUN printf '%s\n' \
      '#!/bin/sh' \
      '# Decode every file of /opt/heif-testcorpus/corpus and print one line per file:' \
      '#   ok      FILE           decoded as a still image' \
      '#   ok-seq  FILE           no still image, decoded as an image sequence (-S --ignore-editlist,' \
      '#                          so that an edit list cannot repeat the content endlessly)' \
      '#   FAIL    FILE: reason   neither worked; reason is the first error line of heif-dec' \
      '# Usage: heif-corpus-sweep [BUILD_DIR]   (default /src/build)' \
      '# Compare the output with /opt/heif-testcorpus/decode-baseline.txt.' \
      'dec="${1:-/src/build}/examples/heif-dec"; out="/tmp/sweep-$$.png"' \
      'cd /opt/heif-testcorpus/corpus || exit 1' \
      'find . -type f \( -iname "*.heic" -o -iname "*.heif" -o -iname "*.avif" -o -iname "*.hif" \) | sort | while read -r f; do' \
      '  if timeout 120 "$dec" --quiet "$f" "$out" >/dev/null 2>&1; then echo "ok      $f"' \
      '  elif timeout 120 "$dec" --quiet -S --ignore-editlist "$f" "$out" >/dev/null 2>&1; then echo "ok-seq  $f"' \
      '  else' \
      '    reason=$(timeout 120 "$dec" "$f" "$out" 2>&1 | grep -v "^Written\|^File contains" | head -1)' \
      '    case "$reason" in *"contain any images"*) reason=$(timeout 120 "$dec" -S --ignore-editlist "$f" "$out" 2>&1 | grep -v "^Written" | head -1);; esac' \
      '    echo "FAIL    $f: $(printf %s "$reason" | cut -c1-120)"' \
      '  fi' \
      '  rm -f /tmp/sweep-$$*.png' \
      'done' \
      > /usr/local/bin/heif-corpus-sweep \
 && chmod +x /usr/local/bin/heif-corpus-sweep \
 && heif-corpus-sweep /src/build > /opt/heif-testcorpus/decode-baseline.txt \
 && echo "corpus baseline: $(grep -c '^ok ' /opt/heif-testcorpus/decode-baseline.txt) ok, $(grep -c '^ok-seq' /opt/heif-testcorpus/decode-baseline.txt) ok-seq, $(grep -c '^FAIL' /opt/heif-testcorpus/decode-baseline.txt) fail"
