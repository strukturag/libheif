# Threat model: libheif

libheif is a C/C++ library that reads and writes HEIF and AVIF image files: ISO/IEC 23008-12
on top of the ISO base media file format (ISO/IEC 14496-12), image sequences, and the
uncompressed image codec ISO/IEC 23001-17 ("unci"). The coded image data inside the
container is handed to codec libraries: libde265, dav1d, libaom, OpenJPEG, OpenJPH,
openh264 and FFmpeg for decoding; x265, kvazaar, libaom, rav1e, SVT-AV1, x264, OpenJPEG and
OpenJPH for encoding.

libheif is the HEIF/AVIF reader behind ImageMagick, GraphicsMagick, libvips (and sharp),
GIMP, Krita, darktable, digiKam, the GNOME and KDE desktops (gdk-pixbuf loader,
thumbnailers, kimageformats), pillow-heif and many other bindings. Any application that
opens an image a user received runs this code on attacker-controlled data.

[SECURITY.md](../SECURITY.md) in the repository root is the authoritative security policy.
This file repeats what the scanner needs and adds what is specific to this image.

## Where untrusted input enters

Every input file is untrusted. The public C API is every header in `libheif/api/libheif/`
except `heif_experimental.h` (`heif.h` includes the others); the API functions are
implemented in `libheif/api/libheif/*.cc`. The entry points that matter:

- `heif_context_read_from_file`, `heif_context_read_from_memory_without_copy` and
  `heif_context_read_from_reader` parse the container (`libheif/box.cc`, `libheif/file.cc`,
  `libheif/file_layout.cc`, `libheif/mini.cc`, `libheif/brands.cc`). Everything that can be
  asked of a `heif_context` afterwards runs on data from that file: image handles,
  properties, metadata (Exif, XMP, MIME items), regions, entity groups, auxiliary images
  (alpha, depth), thumbnails, text items, TAI timestamps, camera matrices.
- `heif_decode_image` decodes an image handle: derived images (grid, overlay, identity,
  tiled, mask in `libheif/image-items/`), the codec glue (`libheif/codecs/`,
  `libheif/plugins/decoder_*.cc`), the pixel buffer (`libheif/image/pixelimage.cc`),
  transformations (crop, rotate, mirror), alpha handling and colour conversion
  (`libheif/color-conversion/`). `heif_image_handle_decode_image_tile` and the tiling API
  in `heif_tiling.h` decode single tiles of grids, tiled items and uncompressed images.
- Sequences: `heif_context_get_track`, `heif_track_decode_next_image` and the rest of
  `heif_sequences.h` (`libheif/sequences/`), including the sample tables and edit lists.
- The uncompressed codec, `libheif/codecs/uncompressed/`, needs no external library and is
  implemented entirely in libheif: arbitrary component layouts, interleaving modes, bit
  depths, tilings, pixel padding and generic compression (deflate, zlib, brotli). Many
  of the advisories of 2026 were in this code, in the derived images and in the colour
  conversion.
- The C++ wrapper `heif_cxx.h` is thin and header-only; it counts as the C API.
- `heif_check_filetype`, `heif_main_brand`, `heif_get_file_mime_type` and the other
  functions that inspect only the first bytes of a file run before any limit is set up.

Four programs in this repository run on untrusted files in production and are in scope:
`heif-dec` and `heif-enc` (`examples/heif_dec.cc`, `examples/heif_enc.cc`) as command-line
tools, `heif-thumbnailer` (`examples/heif_thumbnailer.cc`, registered through
`gnome/heif.thumbnailer`) as the desktop thumbnailer, and the gdk-pixbuf loader
(`gdk-pixbuf/pixbufloader-heif.c`) which GTK applications load for every HEIF or AVIF
image they open. With them, the input readers they share in `heifio/` (JPEG, PNG, TIFF,
Y4M, raw) are in scope: `heif-enc` reads those formats with libjpeg, libpng and libtiff, and
the dimensions, bit depths and plane layouts they report are untrusted.

The encoder side of the library (`heif_encode_image`, `heif_context_add_*`,
`libheif/codecs/*_enc.cc`, `libheif/plugins/encoder_*.cc`, and the box writer) takes images
the application built. The pixel buffers are assumed to follow the API contract
(consistent plane sizes, a chroma format that matches the colorspace, bit depths within
range). Everything else about them is attacker-influenced in practice, because decoded
untrusted images are routinely re-encoded (transcoding services, thumbnail generation):
image dimensions, bit depth, chroma format, presence and depth of an alpha plane, colour
profiles and metadata, and the encoder parameters an application derives from them. Every
valid combination of these must be safe in every encoder plugin.

## Components that matter most and least

Most important, roughly in order:

1. Box parsing and the file model: `libheif/box.cc`, `box.h`, `bitstream.cc`, `file.cc`,
   `file_layout.cc`, `context.cc`, `mini.cc`, `region.cc`, `text.cc`, `compression*.cc`.
   Every length, count, offset and index comes from the file and has to be checked against
   the file size, against every other field it must agree with, and for integer overflow.
2. Derived and composite images: `libheif/image-items/` (grid, overlay, iden, tiled, mask,
   unc_image), including reference chains (`iref`, `dimg`, `auxl`, `thmb`) that can be
   nested, duplicated, shared or cyclic.
3. The uncompressed codec: `libheif/codecs/uncompressed/`.
4. Container-level bitstream parsing: `libheif/codecs/*_boxes.cc` (hvcC, av1C, avcC, vvcC,
   J2K headers, JPEG), `hevc_dec.cc`, `avif_dec.cc` and the other `*_dec.cc`, and the
   decoder plugins in `libheif/plugins/decoder_*.cc`. The container makes claims (size,
   chroma format, bit depth, colour) that the embedded bitstream may contradict, and libheif
   has to reconcile both before and after the codec runs.
5. Sequences: `libheif/sequences/` (sample tables, chunks, edit lists, tracks).
6. Pixel data and colour conversion: `libheif/image/pixelimage.cc`,
   `libheif/color-conversion/*`, `libheif/image/image_description.cc`. These assume that
   plane sizes, bit depths and chroma formats of their inputs are consistent; the checks that
   enforce this live at the entry points, and a missing check there is the usual root cause.
7. The security limits (`libheif/security_limits.cc`, `heif_security.h`) and every place
   that allocates memory or loops over a file-controlled count without consulting them.
8. Encoding: `libheif/plugins/encoder_*.cc`, `libheif/codecs/*_enc.cc`, the encoder API in
   `heif_encoding.h` and the box and file writer. The plugins hand libheif's planes to
   x265, kvazaar, libaom, rav1e, SVT-AV1, x264, OpenJPEG, OpenJPH and libjpeg and have to
   check beforehand what the codec cannot take: sizes above the codec's limits, bit depths
   or chroma formats it does not support, alpha planes with a different depth, odd
   dimensions with subsampled chroma. Past advisories were a crash in x265 at about one
   gigapixel, an overflow in the alpha path of the SVT plugin, and bit depths passed to
   encoders that do not support them.

Less important, or out of scope:

- `heif-dec`, `heif-enc`, `heif-thumbnailer`, the gdk-pixbuf loader and the input readers
  in `heifio/` are in scope, below the library in priority: their own code is small and
  mostly hands data to the library, but it runs on untrusted files in production. The
  other programs in `examples/` (`heif-info`, `heif-view`, `heif-test`, `benchmark`,
  `heif-gen-bayer`, the WebVMT parser in `vmt.cc`) are development and inspection tools;
  bugs in their own code are regular bugs.
- The Go bindings (`go/`) and the Emscripten/WebCodecs code (`decoder_webcodecs.cc`,
  `heif_emscripten.h`), which are not built here.
- `third-party/` holds only download scripts for codec libraries. `fuzzing/` and `tests/`
  are harnesses and tests, not product code.
- The codec libraries themselves (dav1d, libaom, OpenJPEG, OpenJPH, openh264, FFmpeg, x265,
  kvazaar, rav1e, SVT-AV1, x264, libsharpyuv). A crash inside one of them is that project's
  bug and should not be reported here, with two exceptions:
  - libde265 is maintained by the same developer and is built from git master into this
    image (sources in `/opt/libde265/src`, sanitized build linked into `/src/build`).
    Findings in libde265 are welcome. Mark them clearly as libde265 findings and patch
    against `/opt/libde265/src`.
  - When libheif could have rejected the input cheaply before calling the codec, or should
    have checked what the codec returned (declared size versus coded size, declared bit depth
    or chroma format versus the decoded planes, plane sizes that do not match the
    subsampling), report it as a libheif hardening finding and propose that check.

## How to exercise it

The checkout is at `/src`. Three builds exist; rebuild one after a change with
`cmake --build /src/<dir>`. The CMake options used are in `$LIBHEIF_CMAKE_OPTIONS`.

- `/src/build`: Debug with AddressSanitizer and UndefinedBehaviorSanitizer, all codecs,
  example programs and unit tests. Use it for every memory-safety reproducer.
  - `/src/build/examples/heif-info FILE` parses the file and lists its images and tracks;
    `heif-info -d FILE` dumps every box.
  - `/src/build/examples/heif-dec FILE out.png` decodes the primary image (with alpha,
    transformations and colour conversion). `--with-aux` also decodes depth and other
    auxiliary images, `--tiles` decodes every tile separately through the tiling API,
    `-S` decodes an image sequence, `--extract-mime-item TYPE` extracts MIME items.
    Output as `.jpg`, `.png`, `.tif`, `.y4m` exercises different output conversions.
  - `/src/build/examples/heif-enc` produces files to start from: `-A` for AVIF, `-U` for
    uncompressed images (`--unci-compression` for compressed unci), `--mini` for the
    compact box layout, `-S` for sequences, `--jpeg`, `--jpeg2000`, `--avc` for the other
    codecs. A valid file plus a small Python script that patches box fields is the usual way
    to build a reproducer.
  - `/src/build/examples/heif-thumbnailer -s 256 FILE out.png` is the thumbnailer path.
  - The gdk-pixbuf loader is built in both builds (`/src/build/gdk-pixbuf/libpixbufloader-heif.so`
    and `/src/build-release/gdk-pixbuf/libpixbufloader-heif.so`), and `/opt/pixbuf-loaders/`
    holds a loader cache for each. The stock `gdk-pixbuf-thumbnailer` exercises the release
    module: `GDK_PIXBUF_MODULE_FILE=/opt/pixbuf-loaders/build-release.cache
    gdk-pixbuf-thumbnailer -s 128 FILE out.png`. For the sanitized module, preload the ASan
    runtime into the host process:
    `LD_PRELOAD=$(clang -print-resource-dir)/lib/linux/libclang_rt.asan-x86_64.so
    GDK_PIXBUF_MODULE_FILE=/opt/pixbuf-loaders/build.cache gdk-pixbuf-thumbnailer -s 128 FILE
    out.png`. A host program of your own must be linked with `clang++ -fsanitize=address,undefined`
    (the C++ part of the runtime is needed, since libheif and libde265 are C++).
  - `cd /src/build && ctest` runs the unit tests (Catch2, sources in `/src/tests/`, data in
    `/src/tests/data/`). Tests tagged `[.large-memory]` are hidden by default because they
    allocate very large images.
- `/src/build-release`: RelWithDebInfo, no sanitizers, same codecs and example programs.
  Every claim about time or memory must be measured here. The sanitizer build is 20 to 100
  times slower; a decode that takes 100 seconds in `/src/build` can take 0.02 seconds here,
  and such a report has been made before.
- `/src/build-fuzz/fuzzing/`: the libFuzzer harnesses `file_fuzzer` (what OSS-Fuzz runs:
  parse, decode the primary image, all top-level images and thumbnails), `box_fuzzer`,
  `tile_fuzzer`, `sequence_fuzzer`, `api_fuzzer`, `color_conversion_fuzzer` and
  `encoder_fuzzer`. Each runs a single input when given a file: `file_fuzzer crafted.heic`.
  Seeds are in `/src/fuzzing/data/corpus/` and `/src/fuzzing/data/sequence_corpus/`, a
  dictionary in `/src/fuzzing/data/dictionary.txt`. Note that `file_fuzzer` lowers some
  security limits (2 GB total memory, 128 MB per block, 16 Mpixel) before parsing.
- Sample files: `/src/examples/example.heic`, `/src/examples/example.avif`,
  `/src/tests/data/*` (grids, crops, uncompressed layouts, mini files, zero-size boxes),
  `/src/fuzzing/data/corpus/*` (fuzzer seeds, several of them regression inputs for
  fixed bugs).
- The test corpus in `/opt/heif-testcorpus/corpus/` (from
  https://github.com/farindk/heif-testcorpus) holds valid files only: the MPEG HEIF
  conformance files (`nokiatech-heif-conformance`, `mpeggroup-fileformatconformance`: grids,
  overlays, identity transforms, alpha and depth, thumbnails, Exif and XMP, image sequences,
  tiles) and the maintainer's own images for coding formats, bit depths and conformance
  windows that common encoders do not produce (`hevc-high-bitdepth`, `avc-high-bitdepth`,
  `jpeg-rgb-and-high-bitdepth`, `hevc-conformance-window`). The `README.md` of each source
  under `/opt/heif-testcorpus/sources/` gives provenance, licence and known upstream
  defects. These files are the right starting points for crafted inputs: take a file that
  already uses the feature, and change one field.
- `/opt/heif-testcorpus/decode-baseline.txt` records, for every corpus file, whether the
  sanitizer build decoded it when the image was built: `ok` (still image), `ok-seq` (image
  sequence, decoded with `heif-dec -S --ignore-editlist`, because an edit list may repeat
  the content many times) or `FAIL` with the first error line.
  `heif-corpus-sweep [BUILD_DIR]` reproduces that list. Some files are expected to fail:
  features libheif does not implement (`hvt1` tile tracks, `pred` item references,
  layered HEVC `lhv1`) and JPEG variants the system libjpeg rejects (12 bit, 16 bit
  lossless, RGB). What matters is the difference to the baseline.
- Default security limits are in `libheif/security_limits.cc`: 32768 x 32768 pixels per
  image, 4 GB per memory block and in total, 1000 items, 100 children per box, and so on.
  Findings must reproduce with these defaults. `LIBHEIF_SECURITY_LIMITS=off`,
  `--disable-limits` and `heif_security_limits` changes are documented as unsafe for
  untrusted input, and findings that need them are regular bugs.
- Sanitizer defaults in this image: `ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1`.
  Leak reports are off on purpose (codec libraries leak, and leaks are not security
  findings here). Failed allocations return NULL so that libheif's own handling of
  out-of-memory is what gets tested.

## How we rate severity

The precondition for all of these is the public API with the default security limits, and
either a crafted input file or, on the encoder side, a valid image whose dimensions, bit
depth, chroma format, alpha or encoder parameters an attacker can influence.

- Critical: out-of-bounds write, use-after-free, double free, type confusion, or anything
  else that plausibly leads to code execution.
- High: out-of-bounds read whose content can leave the process (through decoded pixels,
  metadata, or an error message); uninitialised memory disclosed the same way; unbounded
  memory or CPU consumption that the security limits do not bound. "Unbounded" means
  amplification: a small file or a small declared image size that costs work or memory out
  of proportion to the declared size (reference chains, edit list repetitions, per-tile
  re-decompression, quadratic loops). Work proportional to a declared size that is within
  the limits is bounded, and is not a finding.
- Medium: null pointer dereference, assertion failure, uncaught exception, division by
  zero or other clean crash; bounded denial of service; out-of-bounds read that cannot be
  observed from outside the process.
- Low: issues that only a sanitizer sees and that have no observable effect, for example
  `memcpy` with a NULL pointer and length zero, signed overflow in a value that is checked
  afterwards, or a misaligned read on x86.
- Not a vulnerability, fixed as a regular bug and without an advisory:
  - anything that needs the security limits disabled or raised;
  - violations of the documented API contract by the caller (NULL where a handle is
    required, inconsistent plane sizes handed to the encoder, use of a freed handle);
  - findings only reachable through the experimental API: functions declared in
    `heif_experimental.h` or compiled under `ENABLE_EXPERIMENTAL_FEATURES` or
    `HEIF_ENABLE_EXPERIMENTAL_FEATURES`. The image builds with that option so that the unit
    tests compile, but production builds must not enable it. Decoding a file through
    `heif-dec` is never experimental; `heif-enc` options marked "(experimental)" in its
    help text are;
  - crashes inside a third-party codec library (except libde265, see above);
  - high but bounded resource use for very large valid images;
  - memory leaks;
  - unsigned integer wrap-around reported by `-fsanitize=integer` without an out-of-bounds
    consequence. The project does fix these as hardening, but they are not vulnerabilities.
    Some wraps are intended, such as `id == UINT32_MAX ? 0 : id + 1`.

## How we would like reports and patches to look

- One report per root cause. If several inputs share a root cause, say so and send one
  report. Reports without a working reproducer are not actionable.
- A reproducer is the crafted file (attached, or as a short Python script that writes it)
  plus the exact command line, the build directory it was run in, the sanitizer output or
  backtrace, and the commit of `/src` it was tested against. Say which security limits were
  in effect. Keep the file as small and as close to a valid file as possible.
- State the impact as one of: out-of-bounds write, out-of-bounds read with disclosure,
  out-of-bounds read without disclosure, use-after-free, crash, unbounded memory, unbounded
  CPU. Name the code location of the missing or wrong check, not only where the crash shows.
- For time or memory findings, give measurements from `/src/build-release`, the file size,
  and the declared image dimensions, so that amplification can be judged.
- Patches: minimal, against `master`, in the project's style (C++20, `m_` prefix for
  members, `Error` return values rather than exceptions, `snake_case` functions,
  `PascalCase` classes). Validate at the entry point (box parser, image item loading, API
  argument check) and return an error; do not "repair" inconsistent files, and do not add
  checks deep in the pixel loops when the entry point can reject the input. Use the
  security-limits mechanism for size and count limits. Beware of integer overflow in the
  check itself; compare by subtraction or widen to 64 bit.
- A Catch2 unit test in `tests/` is welcome when it can be done with a small file. Tests
  that need a specific codec must be guarded by the corresponding `WITH_*` option.
- Before proposing a patch, rebuild (`cmake --build /src/build`), run `ctest --test-dir
  /src/build`, and run `heif-corpus-sweep` and compare with the baseline. A patch that turns
  an `ok` file into a `FAIL` rejects valid input and is wrong, unless that file's source
  README documents it as defective. Say in the report that you did this.
- Do not report what `git log` shows as fixed in the scanned commit. Many hardening changes
  landed in 2026; if a check looks missing, confirm it is missing at the call path you are
  reporting, not only in one of several similar functions.

## Anything to leave alone

- Timings or memory figures taken only from the sanitizer or Debug build.
- Anything that needs `--disable-limits`, `LIBHEIF_SECURITY_LIMITS=off` or raised limits.
- Caller-contract violations on the encoder and image-construction API.
- Memory leaks, and `-fsanitize=integer` unsigned wraps without an out-of-bounds effect.
- Crashes inside dav1d, libaom, OpenJPEG, OpenJPH, openh264, FFmpeg, x265, kvazaar, rav1e,
  SVT-AV1, x264 or libsharpyuv, unless libheif should have caught the inconsistency.
- The Go bindings, the Emscripten code, and the example programs other than `heif-dec`,
  `heif-enc` and `heif-thumbnailer`.
- Spec-conformance or wrong-output issues without a memory-safety or resource consequence.
