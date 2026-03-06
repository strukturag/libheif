/**
 * @preserve libheif.js HEIF decoder
 * (c)2017 struktur AG, http://www.struktur.de, opensource@struktur.de
 *
 * This file is part of libheif
 * https://github.com/strukturag/libheif
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

const assert = require('assert');
const fs = require('fs');

console.log('Running libheif JavaScript tests ...');

const libheif = require('../libheif.js')();

// Test Embind API.
console.log('Loaded libheif.js', libheif.heif_get_version());

// Test internal C API.
assert(libheif.heif_get_version_number_major() === 1, 'libheif major version should be 1')

// Test enum values.
assert(libheif.heif_error_Ok.value === 0, 'heif_error_Ok should be 0')

// Decode the example file and make sure at least one image is returned.
const data = fs.readFileSync('examples/example.heic');
const decoder = new libheif.HeifDecoder();
const image_data = decoder.decode(data);

console.log('Loaded images:', image_data.length);
assert(image_data.length > 0, "Should have loaded images")
