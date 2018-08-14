#!/usr/bin/env bash
set -eufo pipefail
#
# HEIF codec.
# Copyright (c) 2018 struktur AG, Leon Klingele <leon@struktur.de>
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

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" > /dev/null && pwd)"

FILES=$(find "$DIR"/.. -name *.go -exec go fmt "{}" \;)
if [[ -n "$FILES" ]]; then
	echo "The following Go files are not properly formatted:"
	echo "$FILES"
	exit 1
fi
