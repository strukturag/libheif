# Security Policy

libheif parses untrusted input files. Memory-safety bugs in the decoder are treated as
security vulnerabilities and are fixed with priority. This document describes how to report
them, what a useful report contains, and what you can expect from a project that is maintained
by a single, largely unpaid developer.

## Why HEIF files are hard to parse safely

HEIF is built on the ISO base media file format (ISOBMFF), a generic container that is
exceptionally hard to implement securely:

* **Flexibility.** A file is a graph of boxes and items that reference each other: derived images
  (grids, overlays, identity transforms), auxiliary images (alpha, depth), thumbnails, tiles,
  metadata, sequences with sample tables, and properties attached to any of them. Almost every
  feature can be combined with almost every other feature, references can be nested and shared,
  and the decoder has to detect cycles, duplicated references and amplification through
  reference chains, none of which the specification forbids explicitly.
* **Wide value ranges.** Sizes, offsets, counts and dimensions are 32- or 64-bit fields with no
  meaningful upper bounds in the specification. Every field has to be checked not only against
  the file, but against every other field it must be consistent with: declared image size versus
  coded size, transformations (`clap`, `irot`, `imir`) versus tile grids, sample tables (`stsz`,
  `stsc`, `stts`, `stco`) versus each other and versus the file length, and so on. An integer
  overflow in any of these consistency checks becomes a memory-safety bug.
* **Vaguely defined features.** Many features are only loosely specified, or specify what a
  valid file contains but not what a reader should do with an inconsistent one. Files produced
  by real cameras and phones deviate from the specification in practice, so a decoder has to be
  lenient enough to be useful and strict enough to be safe. The uncompressed codec
  (ISO/IEC 23001-17) alone allows nearly arbitrary component layouts, interleaving modes, bit
  depths, tilings and compression schemes.
* **Multiple codecs behind one container.** The container makes claims (dimensions, chroma
  format, bit depth, color information) that the embedded HEVC, AV1, JPEG, JPEG 2000 or VVC
  bitstream may contradict. libheif has to reconcile both before any pixel buffer is touched.

libheif validates all of this, is fuzzed continuously, and uses configurable security limits
to bound memory and CPU use. Still, the number of feature combinations is large, and as
automated vulnerability discovery has improved, the number of reports has grown accordingly
(see [Maintenance capacity](#maintenance-capacity)).

## Reporting a vulnerability

Please do **not** open a public issue for security bugs.

* Preferred: [GitHub private vulnerability reporting](https://github.com/strukturag/libheif/security/advisories/new).
  This creates a private advisory in which the issue can be discussed and through which a CVE
  is assigned when the fix is published.
* Alternative: email dirk.farin@gmail.com with "libheif security" in the subject line.

You will normally receive an acknowledgement within a few business days. Reports are handled
in the evenings and on weekends, see [Maintenance capacity](#maintenance-capacity).

Vulnerabilities in [libde265](https://github.com/strukturag/libde265) are reported the same way
in the libde265 repository. Bugs in other codec libraries (libaom, dav1d, x265, OpenJPEG, ...)
should be reported to those projects.

## What a report must contain

Reports are only actionable when they can be reproduced. Please include:

1. **A reproducer file**: the crafted HEIF/AVIF file, or a minimal program that triggers the
   bug through the public API. A description of a suspected bug without a reproducer is
   treated as a normal bug report, not as a security report.
2. **The libheif version** (release tag or commit hash) and the build configuration
   (CMake preset, codecs, and whether the security limits were changed).
3. **The sanitizer output** (ASan/UBSan/MSan trace) or a debugger backtrace.
4. **Your assessment of the impact**: crash only, out-of-bounds read, out-of-bounds write,
   information disclosure, or resource exhaustion.
5. One report per bug. If you believe that several bugs share a root cause, say so.

Please check the [published advisories](https://github.com/strukturag/libheif/security/advisories)
and test against the **latest release** before reporting.

### Reports produced with AI tools

Most reports received today are found with automated or AI-assisted tools. This is welcome,
but these tools produce many false positives and duplicates, and every report costs real time
to evaluate. Therefore:

* A human must have verified the finding, reproduced it against the current release, and
  written the report. Do not forward raw tool output.
* The requirements above (reproducer file, version, sanitizer trace) apply strictly. Reports
  without a working reproducer are closed without further investigation.
* If your organization runs such tools against libheif at scale, please read
  [Maintenance capacity](#maintenance-capacity) first.

## Scope

**In scope**: memory-safety violations, disclosure of uninitialized memory, and unbounded
resource consumption that can be triggered by decoding or inspecting a crafted input file
through the public API with the **default security limits**. This includes the image, sequence,
metadata, region and uncompressed-codec (`unci`) code paths.

**Handled as regular bugs** (lower priority, usually without an advisory):

* Issues that only occur when the security limits have been disabled or raised
  (`LIBHEIF_SECURITY_LIMITS=off`, `heif_security_limits`, `--disable-limits`). Disabling the
  limits is documented as unsafe for untrusted input.
* Issues that require the caller to violate the documented API contract, for example passing
  inconsistent plane sizes to the encoder. These are still fixed, because libheif tries to
  validate its inputs, but they are not vulnerabilities in libheif.
* Issues in the example programs (`heif-enc`, `heif-dec`, `heif-info`, ...) that are not in the
  library itself, for example in the JPEG/PNG/TIFF/Y4M input readers.
* Issues in APIs that are only available with `ENABLE_EXPERIMENTAL_FEATURES=ON`. These APIs
  are documented as unstable and must not be enabled in production builds.
* High but bounded memory or CPU use for very large valid images. Use the security limits to
  constrain this to what your application needs.

libheif is continuously fuzzed on [OSS-Fuzz](https://google.github.io/oss-fuzz/). Crashes found
by OSS-Fuzz are already known to the maintainer and do not need to be reported again.

## Supported versions

Only the latest release receives security fixes. There are no maintained long-term branches.
Distributions that ship older versions have to backport fixes themselves. Advisories reference
the fixing commits to make this easier.

## Fix and disclosure process

The following are targets, not commitments (see [Maintenance capacity](#maintenance-capacity)):

| Severity | Typical handling |
|----------|------------------|
| Critical: out-of-bounds write or other likely code execution, reachable from a crafted file | Fix and release as soon as possible, usually within days. |
| High: out-of-bounds read with information disclosure, unbounded memory or CPU consumption | Fix on the main branch promptly; released together with other pending fixes, usually within a few weeks. |
| Medium / Low: crash, assertion failure, bounded denial of service | Fix on the main branch; included in the next regular release. |

* Advisories are published on GitHub when the fixed release is available. CVE IDs are requested
  through GitHub.
* The default embargo is 90 days from the report, or shorter when the fix is released earlier.
  Reporters are credited in the advisory unless they ask not to be.
* Pre-release notification of embargoed advisories is available on request to downstream
  distributors and to commercial support customers.
* libheif does not run a bug bounty program and does not pay for reports.
* Priority is decided by severity, not by who reports.

## Maintenance capacity

libheif and libde265 are maintained by one independent developer, largely in unpaid evenings
and weekends. There is no security team.

To make the workload concrete: from January to August 2026, 37 security advisories were
published for libheif (3 rated critical, 13 high), and six releases were made mainly to ship
security fixes. Most of the 2026 reports were found with automated or AI-assisted tools,
often run by organizations that use libheif in their products.
Reproducing, fixing, testing, fuzzing and releasing each fix takes hours to days.

The response times above are therefore best-effort. If your organization depends on libheif and
needs response-time commitments, advisory pre-notification, or dedicated engineering time,
please fund this work:

* [GitHub Sponsors](https://github.com/sponsors/farindk) (organizations can pay by invoice), or
* a commercial support agreement, see [Commercial support](README.md#commercial-support) in the README.
