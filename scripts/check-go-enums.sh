#!/bin/bash
set -e
#
# HEIF codec.
# Copyright (c) 2018 struktur AG, Joachim Bauch <bauch@struktur.de>
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

DEFINE_TYPES="
    heif_error_
    heif_suberror_
    heif_compression_
    heif_chroma_
    heif_colorspace_
    heif_channel_
    "

HEADERS="libheif/api/libheif/heif_error.h libheif/api/libheif/heif_image.h libheif/api/libheif/heif_context.h libheif/api/libheif/heif_color.h"

API_DEFINES=""
for type in $DEFINE_TYPES; do
    DEFINES=$(grep -h "^[ \t]*$type" $HEADERS | sed 's|[[:space:]]*\([^ \t=]*\)[[:space:]]*=.*|\1|g')
    if [ -z "$API_DEFINES" ]; then
        API_DEFINES="$DEFINES"
    else
        API_DEFINES="$API_DEFINES
$DEFINES"
    fi
    ALIASES=$(grep -h "^[ \t]*#define $type" $HEADERS | sed 's|[[:space:]]*#define \([^ \t]*\)[[:space:]]*.*|\1|g')
    if [ ! -z "$ALIASES" ]; then
        API_DEFINES="$API_DEFINES
$ALIASES"
    fi
done
API_DEFINES=$(echo "$API_DEFINES" | sort)

GO_DEFINES=""
for type in $DEFINE_TYPES; do
    DEFINES=$(grep " = C\.$type" go/heif/heif.go | sed 's|.* = C\.\([a-zA-Z0-9_]*\).*|\1|g')
    if [ -z "$GO_DEFINES" ]; then
        GO_DEFINES="$DEFINES"
    else
        GO_DEFINES="$GO_DEFINES
$DEFINES"
    fi
done
GO_DEFINES=$(echo "$GO_DEFINES" | sort)

set +e
CHANGES=$(diff -u <(echo "$API_DEFINES") <(echo "$GO_DEFINES"))
set -e
if [ -z "$CHANGES" ]; then
    echo "All defines from heif.h are present in go/heif/heif.go"
    exit 0
fi

echo "Differences found between enum defines in heif.h and go/heif/heif.go."
echo "Lines prefixed with '+' are only in go/heif/heif.go, resulting in"
echo "compile errors. Lines prefixed with '-' are missing in go/heif/heif.go"
echo
echo "$CHANGES"
exit 1
