# Generated 10-bit HEVC conversion fixtures

`fused-rgba10-422.heic` is a 258 x 129, limited-range 10-bit YCbCr 4:2:2 image.
`fused-rgba10-444.heic` is a 257 x 129, full-range 10-bit YCbCr 4:4:4 image.
Both have opaque output, no source alpha, BT.709 primaries and matrix coefficients,
and the sRGB transfer characteristic. Their widths exercise SIMD tails.

The pixels are generated integer patterns with gradients, discontinuities and
nonzero low bits. No photographs or third-party image content are used.
The generator and generated images are copyright 2026 OlJohnny and distributed
under the MIT license in `tests/generate_color_conversion_fixtures.cc`.

To regenerate, build libheif with tests, internal symbols visible and an x265
encoder supporting 10-bit input:

```sh
cmake --preset=testing -S . -B build-fixtures -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X265=ON
cmake --build build-fixtures --target generate_color_conversion_fixtures
build-fixtures/tests/generate_color_conversion_fixtures tests/data
```

The committed files were generated with libheif 1.23.6 development and x265 4.1.
Encoding uses x265 lossless mode, the medium preset, one frame thread, no worker
pool and no wavefront parallel processing. The pixel pattern and metadata are
deterministic; encoded bytes can vary with the x265 or libheif version. Decoding
the committed files only requires an HEVC decoder, not an encoder.

The fixtures can also be passed to `scripts/benchmark-color-conversion.py` with
`--files-only --file tests/data/fused-rgba10-422.heic --file tests/data/fused-rgba10-444.heic`.
Their small dimensions suit regression tests; camera-sized inputs remain useful
for measuring throughput.
