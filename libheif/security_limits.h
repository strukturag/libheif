/*
 * HEIF codec.
 * Copyright (c) 2018 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
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
#ifndef LIBHEIF_SECURITY_LIMITS_H
#define LIBHEIF_SECURITY_LIMITS_H

#include "libheif/heif.h"
#include <cinttypes>
#include <cstddef>
#include <set>
#include <map>
#include <atomic>
#include <memory>
#include <mutex>
#include "error.h"


extern heif_security_limits global_security_limits;
extern heif_security_limits disabled_security_limits;

// Maximum nesting level of boxes in input files.
// We put a limit on this to avoid unlimited stack usage by malicious input files.
static const int MAX_BOX_NESTING_LEVEL = 20;

static const int MAX_BOX_SIZE = 0x7FFFFFFF; // 2 GB
static const int64_t MAX_LARGE_BOX_SIZE = 0x0FFFFFFFFFFFFFFF;
static const int64_t MAX_FILE_POS = 0x007FFFFFFFFFFFFFLL; // maximum file position
static const int MAX_FRACTION_VALUE = 0x10000;

// Maximum number of 'iovl' overlay images that may be nested inside one another
// along a single decode path. Real files use at most one overlay per decode
// chain, so this is a structural sanity limit, not a common-case constraint.
static const uint32_t MAX_OVERLAY_NESTING_LEVEL = 3;

// Maximum number of input images that a single 'iovl' overlay may composite.
// This is a hardcoded stopgap until a configurable security-limit field can be
// added to heif_security_limits in the next major release (that is an API/ABI
// change and cannot go into a point release).
static const uint32_t MAX_OVERLAY_IMAGES = 5;

// Factor applied to max_items to bound the total number of sub-image decode
// operations triggered by a single top-level decode. Derived images (grid,
// iovl, iden) can reference the same base image through indirection, and the
// cycle-detection set is per-path (it forks at every branch), so a shared
// subtree can be re-decoded once per path that reaches it. Nested sharing makes
// the number of decode operations grow as branch^depth, which is bounded only
// by the recursion depth (<= number of items). Because a well-formed file
// decodes each of its (<= max_items) items a small number of times, capping the
// total at a modest multiple of max_items stops the exponential blow-up while
// leaving every realistic file untouched.  (GHSA-x8xm-cm2c-cfc8, variants V1/V2.)
static const uint32_t MAX_DERIVED_IMAGE_DECODE_FACTOR = 2;

// Maximum number of transformative properties ('irot', 'imir', 'clap') that may be
// applied along a single decode path, i.e. by an item and all the derived images
// (grid, iovl, iden) through which it is reached. Each one is a pass over the whole
// image, 'ipma' can associate up to 255 properties with an item (also the same one
// again and again), and each item of an 'iden' chain applies its own list to the same
// image. HEIF allows one property of each type per item, but chains can add many
// items, so this bounds the total amount of repeated pixel processing.
static const uint32_t MAX_TRANSFORMATIONS_PER_DECODE_PATH = 32;

// How often the pixels of the items that take part in a single top-level decode
// may be decoded on average. The number of decode operations alone does not
// bound the work: an item that is referenced many times is decoded again for
// each reference, and what a reference uses of it can be much smaller than the
// item (e.g. a 'clap' crop). A grid of 660 1x1 tiles, each the same 'clap' of a
// 16384x16384 overlay canvas, stays within the decode-operation budget but builds
// the canvas 660 times. A well-formed file decodes each item a small number of
// times, so the total size of the decoded images is capped at a modest multiple
// of the summed size of the items taking part. A grid that uses one tile item for
// many positions is covered by the size of the grid itself.
// In a usual file, grid tiles are distinct items and overlays do not reuse their inputs,
// so each item is decoded once (factor 1). A grid that repeats one tile item for all
// of its positions comes close to 2.
static const uint32_t MAX_DECODED_PIXELS_FACTOR = 2;

// Small items are cheap to decode however often they are referenced (the number of
// decode operations is bounded separately). The factor above is applied to at least
// this number of pixels, so that it only restricts decodes that take noticeable time.
static const uint64_t MIN_DECODED_PIXELS_BASE = 2048 * 2048;


// Pixel budget of one top-level decode, shared by all copies of its DecodeTraversalState.
// Each decode of an item uses up the size of the item. The budget is MAX_DECODED_PIXELS_FACTOR
// times the summed size of the distinct items that take part in the decode. As many large
// items can be cropped to a small image just like a single one that is used many times,
// that sum counts only up to max_image_size_pixels.
//
// When a single tile of a tiled image is decoded (heif_image_handle_decode_image_tile()),
// each call is a top-level decode of its own, so a budget computed as above would allow
// each tile as much as the whole image. Decoding all tiles would then cost the number of
// tiles times the budget of the whole image. For a tile decode, each item therefore adds
// at most the size of one tile to the summed size of the distinct items, and the minimum
// MIN_DECODED_PIXELS_BASE is divided among the tiles. A tile item that is much larger than
// its tile (e.g. a 'clap' crop of a large image) is then refused, also when it is reached
// through 'iden' items.
class DecodePixelBudget
{
public:
  // max_image_size_pixels is the size limit for a single image from the context's
  // heif_security_limits. It caps the summed size of the distinct items, so
  // the total budget is at most MAX_DECODED_PIXELS_FACTOR times this limit.
  explicit DecodePixelBudget(uint64_t max_image_size_pixels);

  // Budget for decoding one tile of an image that consists of num_tiles tiles of
  // tile_pixels each. Use the constructor above for an image that is a single tile.
  DecodePixelBudget(uint64_t max_image_size_pixels, uint64_t tile_pixels, uint64_t num_tiles);

  // Count one decode of an item against the budget. Every call adds num_pixels to the
  // pixels decoded so far. The item id is used for the allowance: each distinct item adds
  // its (largest) size to it only once, however often it is decoded. A file that decodes
  // each item about once stays far below the budget; one that decodes the same items again
  // and again uses it up.
  // To be called before the item is decoded, with the size of the image (or tile) that is
  // going to be decoded. When that size is not known (0), it has to be called again after
  // the decoding with the size of the decoded image.
  // Returns false when the budget has been exhausted.
  bool count_item_decode(heif_item_id id, uint64_t num_pixels);

private:
  std::mutex m_mutex;

  // The allowance is MAX_DECODED_PIXELS_FACTOR * max(m_distinct_item_pixels, m_min_distinct_item_pixels).
  uint64_t m_max_distinct_item_pixels;                    // cap of m_distinct_item_pixels and of a single decode
  uint64_t m_max_item_pixels;                             // cap of what one item adds to m_distinct_item_pixels
  uint64_t m_min_distinct_item_pixels;                    // floor of m_distinct_item_pixels in the allowance
  std::map<heif_item_id, uint64_t> m_item_pixels_counted; // what each item has added to m_distinct_item_pixels
  uint64_t m_distinct_item_pixels = 0;                    // summed (largest) sizes of the distinct items

  uint64_t m_consumed_pixels = 0;                         // summed sizes of all decodes so far
};


// Traversal state threaded through the derived-image decode recursion for one
// top-level decode. It is copied by value at every hop, which gives the two
// members opposite (and intended) sharing semantics:
//
//   - processed_ids, overlay_nesting and num_transformations are plain values,
//     so each recursion branch gets its own copy. processed_ids is a per-path
//     cycle guard; overlay_nesting counts the overlays nested along the current
//     path; num_transformations counts the transformative properties of the
//     items along the current path.
//
//   - decode_count is a shared_ptr to an atomic, so every copy (including the
//     copies handed to parallel tile-decode threads) shares one global counter
//     that bounds the total number of decode operations. pixel_budget is shared
//     in the same way and bounds the total size of what these operations decode.
//
// A default-constructed DecodeTraversalState (max_decodes == 0) imposes no limit; the
// budget is seeded once at the top level in HeifContext::decode_image().
struct DecodeTraversalState
{
  std::set<heif_item_id> processed_ids;

  uint32_t overlay_nesting = 0;         // number of overlays on the path to here
  uint32_t max_overlay_nesting = 0;     // 0 == unlimited

  uint32_t num_transformations = 0;     // number of transformations on the path to here
  uint32_t max_transformations = 0;     // 0 == unlimited

  std::shared_ptr<std::atomic<uint32_t>> decode_count;
  uint32_t max_decodes = 0;             // 0 == unlimited

  std::shared_ptr<DecodePixelBudget> pixel_budget;  // null == unlimited

  // Count one sub-image decode against the shared budget.
  // Returns false when the budget has been exhausted.
  bool count_decode()
  {
    if (max_decodes == 0 || !decode_count) {
      return true;
    }
    return decode_count->fetch_add(1, std::memory_order_relaxed) < max_decodes;
  }
};


Error check_for_valid_image_size(const heif_security_limits* limits, uint32_t width, uint32_t height);

// Maximum coding-unit size (in pixels) that the given codec may pad a coded
// frame up to. Used as the margin for tighten_image_size_limit_for_ispe.
// For AVC and JPEG, this is 64 instead of their coding-unit size of 16, so that
// the margin also covers the stride alignment of the decoder (see the implementation).
// Returns 0 for codecs without coding-unit padding (e.g. uncompressed).
uint32_t max_coding_unit_size_for_codec(heif_compression_format format);

// Return a copy of `base` with max_image_size_pixels lowered to a value
// just above the declared image size. This is used to bound how much memory
// a codec plugin may allocate for an image whose internal (codec-declared)
// dimensions exceed the file-declared (ispe) dimensions — without us having
// to parse the codec bitstream ourselves.
//
// `coding_unit_size` is the maximum coding-unit size of the target codec
// (e.g. 128 for AV1/VVC, 64 for HEVC), see max_coding_unit_size_for_codec(). The allowed coded
// dimensions are (ispe + coding_unit_size) in each axis, since a codec may
// pad the coded frame up to a coding-unit boundary.
//
// The allowed size never drops below MIN_TIGHTENED_CODED_IMAGE_PIXELS: the
// HEVC/AVC conformance window may crop arbitrarily much, and hardware
// encoders use minimum surface sizes far above tiny image dimensions (issue
// #1856 has a conformant 2x2 HEIC coded as a 160x64 frame). The floor keeps
// the worst-case allocation for a lying bitstream small while accepting such
// padded coded frames; check_decoded_image_size() still validates the
// decoded output against 'ispe' afterwards.
static const uint64_t MIN_TIGHTENED_CODED_IMAGE_PIXELS = 65536;  // 256x256

heif_security_limits tighten_image_size_limit_for_ispe(const heif_security_limits* base,
                                                       uint32_t ispe_width,
                                                       uint32_t ispe_height,
                                                       uint32_t coding_unit_size);


class TotalMemoryTracker
{
public:
  explicit TotalMemoryTracker(const heif_security_limits* limits_context);
  ~TotalMemoryTracker();

  size_t get_max_total_memory_used() const;

  void operator=(const TotalMemoryTracker&) = delete;
  TotalMemoryTracker(const TotalMemoryTracker&) = delete;

private:
  const heif_security_limits* m_limits_context = nullptr;
};


class MemoryHandle
{
public:
  MemoryHandle() = default;
  ~MemoryHandle() { free(); }

  Error alloc(size_t memory_amount, const heif_security_limits* limits_context, const char* reason_description);

  // calloc-style overload: checks `count * element_size` for size_t overflow before allocating.
  // Use this when allocating an array whose total size is count*element_size, to avoid silent
  // truncation on 32-bit builds when count is near UINT32_MAX.
  Error alloc(size_t count, size_t element_size,
              const heif_security_limits* limits_context, const char* reason_description);

  void free();

  void free(size_t memory_amount);

  const heif_security_limits* get_security_limits() const { return m_limits_context; }

  MemoryHandle(const MemoryHandle&) = delete;
  MemoryHandle& operator=(const MemoryHandle&) = delete;

  MemoryHandle(MemoryHandle&& other) noexcept
      : m_limits_context(other.m_limits_context), m_memory_amount(other.m_memory_amount)
  {
    other.m_limits_context = nullptr;
    other.m_memory_amount = 0;
  }

  MemoryHandle& operator=(MemoryHandle&& other) noexcept
  {
    if (this != &other) {
      free();
      m_limits_context = other.m_limits_context;
      m_memory_amount = other.m_memory_amount;
      other.m_limits_context = nullptr;
      other.m_memory_amount = 0;
    }
    return *this;
  }

private:
  const heif_security_limits* m_limits_context = nullptr;
  size_t m_memory_amount = 0;
};


#endif  // LIBHEIF_SECURITY_LIMITS_H
