#!/bin/bash
set -e
#
# HEIF codec.
# Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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
# Verify that every function declared in the public C API headers is actually
# defined (and exported) in the built libheif shared library.
#
# A public declaration whose definition has a mismatched name -- or no
# definition at all -- compiles cleanly and even links into libheif itself,
# because libheif never calls its own public API. Neither the C++ build nor the
# C-header check (scripts/check-c-headers.sh, which only compiles) notices.
# The breakage surfaces only downstream, as an "undefined symbol" link error in
# any program that calls the declared function.
#
# This is exactly what happened in https://github.com/strukturag/libheif/issues/1822:
# heif_properties.h declared  heif_image_get_bayer_pattern_size()  while the
# definition in heif_properties.cc was named  heif_image_has_bayer_pattern().
# The library built and shipped fine; Rust consumers got a link error.
#
# This script extracts every LIBHEIF_API-marked declaration from the public
# headers and checks each name against the defined, exported symbols of a built
# libheif.so (read with nm). Any declared-but-undefined symbol is reported and
# the script exits non-zero.
#
# Usage:
#   check-api-symbols.sh [path/to/libheif.so]
# If no library is given, a minimal libheif (all codecs off, no plugins, no
# examples) is built into a temporary directory. That build needs only cmake
# and a C++ compiler -- no external codec libraries.
#
# heif_experimental.h is excluded: its declarations are compiled only when
# ENABLE_EXPERIMENTAL_FEATURES is on, so they are legitimately absent from a
# default build. heif_cxx.h / heif_emscripten.h are C++-only and declare no
# plain-C ABI symbols; heif_version.h is generated and declares none either.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
API_DIR="$ROOT/libheif/api/libheif"

# Headers whose LIBHEIF_API declarations are not expected in a default build,
# or that declare no plain-C ABI symbols. See the note above.
EXCLUDE="heif_experimental.h heif_cxx.h heif_emscripten.h heif_version.h"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# --- Locate (or build) the libheif shared library ---------------------------

LIB="${1:-}"
if [ -z "$LIB" ]; then
    echo "No library given; building a minimal libheif (all codecs off) ..."
    cmake -S "$ROOT" -B "$TMP/build" \
        -DBUILD_SHARED_LIBS=ON \
        -DWITH_LIBDE265=OFF -DWITH_X265=OFF \
        -DWITH_AOM_DECODER=OFF -DWITH_AOM_ENCODER=OFF \
        -DWITH_X264=OFF -DWITH_OpenH264_DECODER=OFF -DWITH_DAV1D=OFF \
        -DWITH_EXAMPLES=OFF -DWITH_GDK_PIXBUF=OFF \
        -DBUILD_TESTING=OFF -DENABLE_PLUGIN_LOADING=OFF \
        >"$TMP/cmake.log" 2>&1 \
        && cmake --build "$TMP/build" -j"$(nproc)" >>"$TMP/cmake.log" 2>&1 \
        || { echo "ERROR: minimal libheif build failed:" >&2; cat "$TMP/cmake.log" >&2; exit 1; }
    LIB="$TMP/build/libheif/libheif.so"
fi

if [ ! -e "$LIB" ]; then
    echo "ERROR: libheif shared library not found: $LIB" >&2
    exit 1
fi

# --- Collect declared API symbols and defined library symbols ---------------

# Declared symbols: every identifier introduced by a LIBHEIF_API declaration.
# For a function this is the identifier just before the argument list '(';
# for an exported data object (e.g. heif_error_success) it is the last
# identifier before the terminating ';'. Comments and preprocessor lines are
# stripped first so that commented-out declarations and the macro definition
# itself are ignored.
find_args=()
for h in $EXCLUDE; do find_args+=( ! -name "$h" ); done
find "$API_DIR" -maxdepth 1 -name '*.h' "${find_args[@]}" -print0 \
  | xargs -0 cat \
  | perl -0777 -ne '
        s{//[^\n]*|/\*.*?\*/}{}gs;   # strip line and block comments
        s/^\s*#.*$//mg;              # strip preprocessor directives
        while (/\bLIBHEIF_API\b\s+(.*?);/gs) {
            my $d = $1;
            if    ($d =~ /(\w+)\s*\(/) { print "$1\n"; }   # function
            elsif ($d =~ /(\w+)\s*$/)  { print "$1\n"; }   # data object
        }' \
  | sort -u > "$TMP/declared.txt"

nm -D --defined-only "$LIB" | awk '{print $NF}' | sort -u > "$TMP/defined.txt"

declared_count=$(wc -l < "$TMP/declared.txt")
missing=$(comm -23 "$TMP/declared.txt" "$TMP/defined.txt")

# --- Report ------------------------------------------------------------------

echo ""
if [ -n "$missing" ]; then
    missing_count=$(echo "$missing" | wc -l)
    echo "API symbol check FAILED: $missing_count of $declared_count declared API symbols"
    echo "are NOT defined in the built library ($(basename "$LIB")):"
    echo "$missing" | sed 's/^/    /'
    echo ""
    echo "Each name above is declared (with LIBHEIF_API) in a public header but has"
    echo "no matching definition. The usual cause is a definition whose name does"
    echo "not match the declaration (see issue #1822)."
    exit 1
fi

echo "API symbol check passed ($declared_count declared API symbols, all defined)."
