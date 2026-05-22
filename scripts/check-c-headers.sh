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
# Verify that the public API headers are valid, plain C.
#
# The C++ build does not catch C-only mistakes such as referring to a struct
# type by its bare tag name (`heif_bad_pixel` instead of `struct heif_bad_pixel`),
# because C++ injects struct tags into the ordinary namespace while C does not.
# Such a mistake compiles fine in our C++ test suite but breaks every C consumer
# (see https://github.com/strukturag/libheif/pull/1812).
#
# This script compiles each public header on its own as strict C. Besides
# catching C++-isms, testing headers independently also verifies they are
# self-contained (pull in their own dependencies). Any header that fails to
# parse as C is reported and the script exits non-zero.
#
# Exception: heif_error.h is currently NOT self-contained -- it uses the
# LIBHEIF_API macro but cannot include heif_library.h, because the two headers
# are mutually dependent. It is therefore tested with the <libheif/heif.h>
# umbrella prelude instead of standalone.
# TODO (v1.23.0): once the LIBHEIF_API macro is factored out into its own
# dependency-free header (see the TODO in heif_library.h), remove this
# exception so heif_error.h is checked standalone like every other header.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
API_DIR="$ROOT/libheif/api/libheif"

# Headers that are intentionally C++ only and must NOT be checked as C.
CXX_ONLY="heif_cxx.h heif_emscripten.h"

# Headers that are not (yet) self-contained and must be tested with the
# <libheif/heif.h> umbrella prelude instead of standalone. See the TODO above.
NEEDS_PRELUDE="heif_error.h"

# C standards to validate against. C99 is intentionally excluded: the headers
# legitimately repeat identical typedefs (e.g. heif_image_handle), which C11
# permits but C99 forbids.
C_STANDARDS="c11 c17"

# Collect available C compilers (gcc is enough; clang catches extra cases).
COMPILERS=()
command -v gcc   >/dev/null 2>&1 && COMPILERS+=(gcc)
command -v clang >/dev/null 2>&1 && COMPILERS+=(clang)
if [ ${#COMPILERS[@]} -eq 0 ]; then
    echo "ERROR: no C compiler (gcc or clang) found in PATH." >&2
    exit 1
fi

WARN_FLAGS="-pedantic -Wall -Wextra -Werror"

# Build a self-contained include tree <tmp>/libheif/ so that the angle-bracket
# includes used by the headers (#include <libheif/...>) resolve, and generate
# heif_version.h from its template (normally produced by CMake at build time).
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/libheif"
cp "$API_DIR"/*.h "$TMP/libheif/"

VERSION=$(grep -m1 -E '^\s*project\(' "$ROOT/CMakeLists.txt" \
          | grep -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' | awk '{print $2}')
V_MAJOR=${VERSION%%.*}
V_REST=${VERSION#*.}
V_MINOR=${V_REST%%.*}
V_PATCH=${V_REST##*.}
sed -e "s/@PROJECT_VERSION_MAJOR@/${V_MAJOR:-0}/g" \
    -e "s/@PROJECT_VERSION_MINOR@/${V_MINOR:-0}/g" \
    -e "s/@PROJECT_VERSION_PATCH@/${V_PATCH:-0}/g" \
    -e "s|@PLUGIN_DIRECTORY@||g" \
    "$API_DIR/heif_version.h.in" > "$TMP/libheif/heif_version.h"

failures=0
checked=0

for header_path in "$TMP"/libheif/*.h; do
    header=$(basename "$header_path")
    case " $CXX_ONLY heif_version.h " in
        *" $header "*) continue ;;
    esac

    # Test each header on its own, so we also catch headers that are not
    # self-contained. Listed exceptions get the umbrella prelude first.
    src="$TMP/check_${header%.h}.c"
    {
        case " $NEEDS_PRELUDE " in
            *" $header "*) echo "#include <libheif/heif.h>" ;;
        esac
        echo "#include <libheif/$header>"
        echo "int main(void) { return 0; }"
    } > "$src"

    for cc in "${COMPILERS[@]}"; do
        for std in $C_STANDARDS; do
            checked=$((checked + 1))
            if ! err=$("$cc" -std="$std" $WARN_FLAGS -I"$TMP" -c "$src" -o /dev/null 2>&1); then
                echo "FAIL: $header is not valid C ($cc -std=$std)"
                echo "$err" | sed 's/^/    /'
                failures=$((failures + 1))
            fi
        done
    done
done

echo ""
if [ "$failures" -ne 0 ]; then
    echo "C API header check FAILED: $failures of $checked compilations failed."
    echo "The public headers must be valid plain C. A common cause is using a"
    echo "struct type by its bare tag (e.g. 'heif_foo*') instead of either a"
    echo "typedef name or 'struct heif_foo*'."
    exit 1
fi

echo "C API header check passed ($checked compilations, compilers: ${COMPILERS[*]})."
