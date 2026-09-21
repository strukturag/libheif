### Incorrect decoding of negative 16-bit HEIF overlay offsets

### Summary

`ImageItem_Overlay::readvec_signed()` incorrectly decodes negative 16-bit overlay offsets.

The vulnerable code uses a fixed `0x7fffffff` mask when converting negative values:

``` cpp
if (negative) {
    return -static_cast<int32_t>((~val) & 0x7fffffff) - 1;
}
```

For a 16-bit value:

``` text
0xFFFF
```

the expected signed value is:

``` text
-1
```

but the vulnerable implementation decodes it as:

``` text
-2147418113
```

This causes a valid negative overlay offset to be interpreted as being far outside the image canvas, causing the overlay to be silently discarded.

### Affected Code

``` text
libheif/image-items/overlay.cc
ImageItem_Overlay::readvec_signed()
```

### Vulnerable Commit

``` text
ab0565a6
```

### Reproduction

A minimal 377-byte HEIF PoC is attached:

``` text
overlay-negative-16bit.heif
```

Build the vulnerable version:

``` bash
rm -rf build-vuln

CC=clang CXX=clang++ cmake -S . -B build-vuln \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_EXAMPLES=ON \
    -DWITH_AOM_DECODER=OFF \
    -DWITH_AOM_ENCODER=OFF \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build build-vuln -j$(nproc)
```

Run the PoC:

``` bash
./build-vuln/examples/heif-dec \
    overlay-negative-16bit.heif \
    /tmp/overlay-vulnerable.png
```

The vulnerable build produces an 8x8 composite image where the overlay is missing.

### Expected Behavior

The encoded 16-bit offset:

``` text
0xFFFF
```

represents:

``` text
-1
```

The overlay should therefore be positioned at:

``` text
x = -1
y = 0
```

The child image should be partially visible after clipping.

### Actual Behavior

The vulnerable parser converts:

``` text
0xFFFF
```

to approximately:

``` text
-2147418113
```

The overlay is then considered outside the destination canvas and is discarded.

### Vulnerable Output

The vulnerable build produced:

``` text
size: (8, 8)

(0,0): (0, 0, 0)
(6,0): (0, 0, 0)
(7,0): (0, 0, 0)

SHA256:

80bc23f635d76d4178844b66760d23d0028d7d3dd47f93b22ca099e2cd2f7e35
```

### Fixed Output

The same PoC was decoded using the fixed build.

The fixed build produced:

``` text
size: (8, 8)

(0,0): (127, 127, 127)
(6,0): (127, 127, 127)
(7,0): (0, 0, 0)

SHA256:

f4459251da9804419d6eeb6a820e1ac33aaba5f2e83dcefc9491ae16e820dd56
```

This demonstrates a deterministic difference between the vulnerable and fixed implementations using the exact same input file.

### Impact

A crafted HEIF file can cause valid overlay content to be incorrectly omitted during decoding.

I did not observe:

- memory corruption
- out-of-bounds access
- crash
- information disclosure
- arbitrary code execution

The demonstrated impact is incorrect image composition / decoding integrity.

### Fix

The signed conversion should use a mask corresponding to the actual field width instead of the fixed `0x7fffffff` mask.

A regression test for the 16-bit negative offset was added, together with a 32-bit negative-offset control case.

The fixed test suite passes:

``` text
5 test cases
57 assertions
```

### PoC

``` text
overlay-negative-16bit.heif
Size: 377 bytes

SHA256:
698d89e869360a5c1086abeada886601a280ba60ba33d1eab1204a849a296301
```

### Disclosure

This report is being submitted privately with the PoC attached.

I did not observe a memory-safety impact during testing. I would appreciate the maintainer's assessment of the security impact and whether the issue qualifies for a security advisory or CVE.
