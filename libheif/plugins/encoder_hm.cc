/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
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

// The variants of HM
// ------------------
// The plugin can contain two versions of the HEVC reference software HM, which are selected
// with CMake options:
//
// ENABLE_HM_VARIANT_LATEST  The latest version of HM.
// ENABLE_HM_VARIANT_SCC     The latest version of HM that has the screen content coding (SCC)
//                           tools. They never became part of the main line of HM.
//
// When both are compiled in, the SCC variant encodes the images that use an SCC tool, and
// the latest version encodes all others. The SCC variant also has everything that this
// plugin needs for images without SCC tools, so it takes over when it is the only variant.
//
// All code that depends on HM is in encoder_hm_variant.cc, which is compiled once for each
// variant. Here, a variant is a function that takes HM command line options and a picture.

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "encoder_hm.h"
#include "encoder_hm_variant.h"
#include "encoder_input_check.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>


static const char* kError_out_of_memory = "Out of memory";
static const char* kError_no_sequences = "The HM encoder plugin encodes images, but no image sequences";

// HM is chosen explicitly, for what the other encoders do not support. Its priority is
// lower than theirs, so that it never becomes the default HEVC encoder by accident.
static const int HM_PLUGIN_PRIORITY = 10;

// 'hvcC' stores the bit depths in fields of 3 bits.
static const int HM_MAX_BIT_DEPTH = 15;

static const char* kParam_chroma = "chroma";
static const char* const kParam_chroma_valid_values[] = {
  "420", "422", "444", nullptr
};

// The screen content coding profiles of HEVC that HM supports end at 10 bits.
static const int HM_MAX_BIT_DEPTH_SCREEN_CONTENT_CODING = 10;

enum hm_tool_set
{
  // Coding tools of the HEVC range extensions. Table A.2 of ITU-T H.265 only allows them
  // in the profiles for 4:4:4.
  hm_tool_set_range_extensions,

  // Coding tools of the HEVC screen content coding extensions. Only a variant of HM with
  // these tools has them, and they need one of the screen content coding profiles.
  hm_tool_set_screen_content_coding
};

// The coding tools keep the defaults of HEVC version 1, since not every decoder implements
// all of the others.
struct hm_coding_tool
{
  const char* parameter_name;
  const char* hm_option;
  bool default_value;
  hm_tool_set tool_set;

  // The option of HM disables the tool.
  bool hm_option_is_inverted;
};

static const hm_coding_tool hm_coding_tools[] = {
  {"cross-component-prediction", "CrossComponentPrediction",      false, hm_tool_set_range_extensions,      false},
  {"implicit-rdpcm",             "ImplicitResidualDPCM",          false, hm_tool_set_range_extensions,      false},
  {"explicit-rdpcm",             "ExplicitResidualDPCM",          false, hm_tool_set_range_extensions,      false},
  {"transform-skip-rotation",    "ResidualRotation",              false, hm_tool_set_range_extensions,      false},
  {"transform-skip-context",     "SingleSignificanceMapContext",  false, hm_tool_set_range_extensions,      false},
  {"persistent-rice-adaptation", "GolombRiceParameterAdaptation", false, hm_tool_set_range_extensions,      false},
  {"intra-smoothing",            "IntraReferenceSmoothing",       true,  hm_tool_set_range_extensions,      false},
  {"extended-precision",         "ExtendedPrecision",             false, hm_tool_set_range_extensions,      false},
  {"cabac-bypass-alignment",     "AlignCABACBeforeBypass",        false, hm_tool_set_range_extensions,      false},
  {"palette-mode",               "PaletteMode",                   false, hm_tool_set_screen_content_coding, false},
  {"intra-block-copy",           "IntraBlockCopyEnabled",         false, hm_tool_set_screen_content_coding, false},
  {"adaptive-colour-transform",  "ColourTransform",               false, hm_tool_set_screen_content_coding, false},
  {"intra-boundary-filter",      "IntraBoundaryFilterDisabled",   true,  hm_tool_set_screen_content_coding, true},
};

static const int HM_NUM_CODING_TOOLS = sizeof(hm_coding_tools) / sizeof(hm_coding_tools[0]);

enum
{
  hm_tool_intra_smoothing = 6,
  hm_tool_extended_precision = 7,
  hm_tool_cabac_bypass_alignment = 8,
  hm_tool_adaptive_colour_transform = 11
};

static const char* kParam_transform_skip_max_size = "transform-skip-log2-max-size";


struct encoder_struct_hm
{
  // --- parameters

  int quality = 50;
  bool lossless = false;
  heif_chroma chroma = heif_chroma_420;

  // Also holds the tools that no variant of HM in this plugin has. They keep their defaults.
  bool coding_tool[HM_NUM_CODING_TOOLS] = {};
  int transform_skip_log2_max_size = 2;

  encoder_struct_hm()
  {
    for (int i = 0; i < HM_NUM_CODING_TOOLS; i++) {
      coding_tool[i] = hm_coding_tools[i].default_value;
    }
  }

  // --- output

  std::deque<std::vector<uint8_t>> output_data;

  std::vector<uint8_t> active_data; // holds the data that we just returned

  std::string error_message; // holds the message of the error that we just returned
};


// HM keeps part of its state in global variables. Each variant has its own, but one lock
// for all of them is enough.
static std::mutex hm_mutex;


// --- variants of HM

static const hm_variant* hm_variant_latest()
{
#if HAVE_HM_VARIANT_LATEST
  return get_hm_variant_latest();
#else
  return nullptr;
#endif
}

static const hm_variant* hm_variant_screen_content_coding()
{
#if HAVE_HM_VARIANT_SCC
  return get_hm_variant_scc();
#else
  return nullptr;
#endif
}

#if !HAVE_HM_VARIANT_LATEST && !HAVE_HM_VARIANT_SCC
#error "The HM encoder plugin needs at least one variant of HM"
#endif

static bool hm_is_coding_tool_available(const hm_coding_tool& tool)
{
  return (tool.tool_set != hm_tool_set_screen_content_coding ||
          hm_variant_screen_content_coding() != nullptr);
}


#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

static void hm_set_default_parameters(void* encoder);


static const char* hm_plugin_name()
{
  std::string versions;
  for (const hm_variant* variant : {hm_variant_latest(), hm_variant_screen_content_coding()}) {
    if (variant) {
      versions += (versions.empty() ? "" : " + ");
      versions += variant->version;
    }
  }

  snprintf(plugin_name, MAX_PLUGIN_NAME_LENGTH, "HM HEVC reference encoder %s (experimental)", versions.c_str());
  return plugin_name;
}


#define MAX_NPARAMETERS 20

static heif_encoder_parameter hm_encoder_params[MAX_NPARAMETERS];
static const heif_encoder_parameter* hm_encoder_parameter_ptrs[MAX_NPARAMETERS + 1];

static void hm_init_parameters()
{
  heif_encoder_parameter* p = hm_encoder_params;
  const heif_encoder_parameter** d = hm_encoder_parameter_ptrs;
  int i = 0;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = heif_encoder_parameter_name_quality;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 50;
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 0;
  p->integer.maximum = 100;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = heif_encoder_parameter_name_lossless;
  p->type = heif_encoder_parameter_type_boolean;
  p->boolean.default_value = false;
  p->has_default = true;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_chroma;
  p->type = heif_encoder_parameter_type_string;
  p->string.default_value = "420";
  p->has_default = true;
  p->string.valid_values = kParam_chroma_valid_values;
  d[i++] = p++;

  for (const hm_coding_tool& tool : hm_coding_tools) {
    if (!hm_is_coding_tool_available(tool)) {
      continue;
    }

    assert(i < MAX_NPARAMETERS);
    p->version = 2;
    p->name = tool.parameter_name;
    p->type = heif_encoder_parameter_type_boolean;
    p->boolean.default_value = tool.default_value;
    p->has_default = true;
    d[i++] = p++;
  }

  assert(i < MAX_NPARAMETERS);
  p->version = 2;
  p->name = kParam_transform_skip_max_size;
  p->type = heif_encoder_parameter_type_integer;
  p->integer.default_value = 2;
  p->has_default = true;
  p->integer.have_minimum_maximum = true;
  p->integer.minimum = 2;
  p->integer.maximum = 5;
  p->integer.valid_values = NULL;
  p->integer.num_valid_values = 0;
  d[i++] = p++;

  assert(i < MAX_NPARAMETERS + 1);
  d[i++] = nullptr;
}


static const heif_encoder_parameter** hm_list_parameters(void* encoder)
{
  return hm_encoder_parameter_ptrs;
}


static void hm_init_plugin()
{
  hm_init_parameters();
}


static void hm_cleanup_plugin()
{
}


static heif_error hm_new_encoder(void** enc)
{
  encoder_struct_hm* encoder = new(std::nothrow) encoder_struct_hm();
  if (!encoder) {
    return heif_error{heif_error_Memory_allocation_error, heif_suberror_Unspecified, kError_out_of_memory};
  }

  *enc = encoder;

  hm_set_default_parameters(encoder);

  return heif_error_ok;
}

static void hm_free_encoder(void* encoder_raw)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  delete encoder;
}


static int hm_find_coding_tool(const char* name)
{
  for (int i = 0; i < HM_NUM_CODING_TOOLS; i++) {
    if (hm_is_coding_tool_available(hm_coding_tools[i]) &&
        strcmp(name, hm_coding_tools[i].parameter_name) == 0) {
      return i;
    }
  }

  return -1;
}


static heif_error hm_set_parameter_quality(void* encoder_raw, int quality)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (quality < 0 || quality > 100) {
    return heif_error_invalid_parameter_value;
  }

  encoder->quality = quality;

  return heif_error_ok;
}

static heif_error hm_get_parameter_quality(void* encoder_raw, int* quality)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  *quality = encoder->quality;

  return heif_error_ok;
}

static heif_error hm_set_parameter_lossless(void* encoder_raw, int enable)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  encoder->lossless = enable ? 1 : 0;

  return heif_error_ok;
}

static heif_error hm_get_parameter_lossless(void* encoder_raw, int* enable)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  *enable = encoder->lossless;

  return heif_error_ok;
}

static heif_error hm_set_parameter_logging_level(void* encoder_raw, int logging)
{
  return heif_error_ok;
}

static heif_error hm_get_parameter_logging_level(void* encoder_raw, int* loglevel)
{
  *loglevel = 0;

  return heif_error_ok;
}


static heif_error hm_set_parameter_integer(void* encoder_raw, const char* name, int value)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return hm_set_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return hm_set_parameter_lossless(encoder, value);
  }
  else if (strcmp(name, kParam_transform_skip_max_size) == 0) {
    if (value < 2 || value > 5) {
      return heif_error_invalid_parameter_value;
    }

    encoder->transform_skip_log2_max_size = value;
    return heif_error_ok;
  }

  int tool = hm_find_coding_tool(name);
  if (tool >= 0) {
    encoder->coding_tool[tool] = (value != 0);
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}

static heif_error hm_get_parameter_integer(void* encoder_raw, const char* name, int* value)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (strcmp(name, heif_encoder_parameter_name_quality) == 0) {
    return hm_get_parameter_quality(encoder, value);
  }
  else if (strcmp(name, heif_encoder_parameter_name_lossless) == 0) {
    return hm_get_parameter_lossless(encoder, value);
  }
  else if (strcmp(name, kParam_transform_skip_max_size) == 0) {
    *value = encoder->transform_skip_log2_max_size;
    return heif_error_ok;
  }

  int tool = hm_find_coding_tool(name);
  if (tool >= 0) {
    *value = encoder->coding_tool[tool];
    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}


static heif_error hm_set_parameter_string(void* encoder_raw, const char* name, const char* value)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (strcmp(name, kParam_chroma) == 0) {
    if (strcmp(value, "420") == 0) {
      encoder->chroma = heif_chroma_420;
      return heif_error_ok;
    }
    else if (strcmp(value, "422") == 0) {
      encoder->chroma = heif_chroma_422;
      return heif_error_ok;
    }
    else if (strcmp(value, "444") == 0) {
      encoder->chroma = heif_chroma_444;
      return heif_error_ok;
    }
    else {
      return heif_error_invalid_parameter_value;
    }
  }

  return heif_error_unsupported_parameter;
}

static void save_strcpy(char* dst, int dst_size, const char* src)
{
  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = 0;
}

static heif_error hm_get_parameter_string(void* encoder_raw, const char* name,
                                          char* value, int value_size)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (strcmp(name, kParam_chroma) == 0) {
    switch (encoder->chroma) {
      case heif_chroma_420:
        save_strcpy(value, value_size, "420");
        break;
      case heif_chroma_422:
        save_strcpy(value, value_size, "422");
        break;
      case heif_chroma_444:
        save_strcpy(value, value_size, "444");
        break;
      default:
        assert(false);
        return heif_error_invalid_parameter_value;
    }

    return heif_error_ok;
  }

  return heif_error_unsupported_parameter;
}


static void hm_set_default_parameters(void* encoder)
{
  for (const heif_encoder_parameter** p = hm_encoder_parameter_ptrs; *p; p++) {
    const heif_encoder_parameter* param = *p;

    if (param->has_default) {
      switch (param->type) {
        case heif_encoder_parameter_type_integer:
          hm_set_parameter_integer(encoder, param->name, param->integer.default_value);
          break;
        case heif_encoder_parameter_type_boolean:
          hm_set_parameter_integer(encoder, param->name, param->boolean.default_value);
          break;
        case heif_encoder_parameter_type_string:
          hm_set_parameter_string(encoder, param->name, param->string.default_value);
          break;
      }
    }
  }
}


static void hm_query_input_colorspace(heif_colorspace* colorspace, heif_chroma* chroma)
{
  if (*colorspace == heif_colorspace_monochrome) {
    *colorspace = heif_colorspace_monochrome;
    *chroma = heif_chroma_monochrome;
  }
  else {
    *colorspace = heif_colorspace_YCbCr;
    *chroma = heif_chroma_420;
  }
}


static void hm_query_input_colorspace2(void* encoder_raw, heif_colorspace* colorspace, heif_chroma* chroma)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (*colorspace == heif_colorspace_monochrome) {
    *colorspace = heif_colorspace_monochrome;
    *chroma = heif_chroma_monochrome;
  }
  else {
    *colorspace = heif_colorspace_YCbCr;
    *chroma = encoder->chroma;
  }
}


// The size that is visible after the conformance window has been applied. The padding to
// the size of the coding blocks is done by HM and does not show up here.
static void hm_query_encoded_size(void* encoder_raw, uint32_t input_width, uint32_t input_height,
                                  uint32_t* encoded_width, uint32_t* encoded_height)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  *encoded_width = input_width;
  *encoded_height = input_height;

  // We do not know here whether the image is monochrome. Rounding a monochrome image too
  // does no harm.

  if (encoder->chroma == heif_chroma_420 || encoder->chroma == heif_chroma_422) {
    *encoded_width = (input_width + 1) & ~1U;
  }

  if (encoder->chroma == heif_chroma_420) {
    *encoded_height = (input_height + 1) & ~1U;
  }
}


// --- configuration of HM

static std::string hm_option(const char* name, const std::string& value)
{
  return std::string("--") + name + "=" + value;
}

static std::string hm_option(const char* name, int value)
{
  return hm_option(name, std::to_string(value));
}


// Table A.8 of ITU-T H.265. The width and the height of the picture are limited to
// Sqrt(8 * MaxLumaPs).
static const char* hm_level_for_picture_size(uint32_t width, uint32_t height)
{
  struct level
  {
    const char* name;
    uint64_t max_luma_picture_size;
  };

  static const level levels[] = {
    {"1",   36864},
    {"2",   122880},
    {"2.1", 245760},
    {"3",   552960},
    {"3.1", 983040},
    {"4",   2228224},
    {"5",   8912896},
    {"6",   35651584}
  };

  const uint64_t w = width;
  const uint64_t h = height;

  for (const level& l : levels) {
    if (w * h <= l.max_luma_picture_size &&
        w * w <= 8 * l.max_luma_picture_size &&
        h * h <= 8 * l.max_luma_picture_size) {
      return l.name;
    }
  }

  return "8.5"; // no limits
}


static bool hm_uses_coding_tools_of(const encoder_struct_hm* encoder, hm_tool_set tool_set)
{
  for (int i = 0; i < HM_NUM_CODING_TOOLS; i++) {
    if (hm_coding_tools[i].tool_set == tool_set &&
        encoder->coding_tool[i] != hm_coding_tools[i].default_value) {
      return true;
    }
  }

  return false;
}


// The variant of HM that encodes the image.
static const hm_variant* hm_select_variant(const encoder_struct_hm* encoder)
{
  if (hm_uses_coding_tools_of(encoder, hm_tool_set_screen_content_coding) ||
      hm_variant_latest() == nullptr) {
    return hm_variant_screen_content_coding();
  }

  return hm_variant_latest();
}


// The profile with the tightest constraints that the image fits into. All our pictures
// are intra pictures.
static const char* hm_profile(const encoder_struct_hm* encoder, heif_chroma chroma, int bit_depth)
{
  if (hm_uses_coding_tools_of(encoder, hm_tool_set_screen_content_coding)) {
    // HM chooses one of the Screen-Extended Main profiles by the bit depth and the chroma format.
    return "main-SCC";
  }

  bool needs_444_profile = hm_uses_coding_tools_of(encoder, hm_tool_set_range_extensions);

  if (encoder->transform_skip_log2_max_size != 2) {
    needs_444_profile = true;
  }

  if (bit_depth > 12 || encoder->coding_tool[hm_tool_extended_precision]) {
    if (chroma == heif_chroma_monochrome && !needs_444_profile) {
      return "monochrome16";
    }

    return "main_444_16_intra";
  }

  if (chroma == heif_chroma_444 || needs_444_profile) {
    return bit_depth <= 8 ? "main_444_intra" : bit_depth <= 10 ? "main_444_10_intra" : "main_444_12_intra";
  }

  switch (chroma) {
    case heif_chroma_monochrome:
      return bit_depth <= 8 ? "monochrome" : "monochrome12";
    case heif_chroma_422:
      return bit_depth <= 10 ? "main_422_10_intra" : "main_422_12_intra";
    default:
      return bit_depth <= 8 ? "main" : bit_depth <= 10 ? "main10" : "main_12_intra";
  }
}


// The screen content coding profiles are narrower than the range extensions profiles.
// Returns an empty string when the image can be encoded with the coding tools that are set.
static std::string hm_check_screen_content_coding(const encoder_struct_hm* encoder, heif_chroma chroma, int bit_depth)
{
  if (!hm_uses_coding_tools_of(encoder, hm_tool_set_screen_content_coding)) {
    return {};
  }

  if (bit_depth > HM_MAX_BIT_DEPTH_SCREEN_CONTENT_CODING) {
    return "The screen content coding tools can only be used with up to 10 bits per sample.";
  }

  if (chroma == heif_chroma_422) {
    return "The screen content coding tools cannot be used with chroma 4:2:2.";
  }

  if (encoder->coding_tool[hm_tool_adaptive_colour_transform] && chroma != heif_chroma_444) {
    return "The adaptive colour transform can only be used with chroma 4:4:4.";
  }

  if (encoder->coding_tool[hm_tool_extended_precision] ||
      encoder->coding_tool[hm_tool_cabac_bypass_alignment]) {
    return "The screen content coding tools cannot be combined with 'extended-precision' or 'cabac-bypass-alignment'.";
  }

  return {};
}


static std::vector<std::string> hm_options(const encoder_struct_hm* encoder, const hm_variant* variant,
                                           const heif_image* image,
                                           heif_image_input_class input_class,
                                           uint32_t width, uint32_t height)
{
  const heif_chroma chroma = heif_image_get_chroma_format(image);
  const int bit_depth = heif_image_get_bits_per_pixel_range(image, heif_channel_Y);

  const char* chroma_format = (chroma == heif_chroma_monochrome ? "400" :
                               chroma == heif_chroma_420 ? "420" :
                               chroma == heif_chroma_422 ? "422" : "444");

  std::vector<std::string> options;

  // HM insists on these two names. We do not open the files.
  options.push_back(hm_option("InputFile", "memory"));
  options.push_back(hm_option("BitstreamFile", "memory"));

  // --- the picture

  options.push_back(hm_option("SourceWidth", static_cast<int>(width)));
  options.push_back(hm_option("SourceHeight", static_cast<int>(height)));
  options.push_back(hm_option("InputBitDepth", bit_depth));
  options.push_back(hm_option("InternalBitDepth", bit_depth));
  options.push_back(hm_option("InputChromaFormat", chroma_format));
  options.push_back(hm_option("ChromaFormatIDC", chroma_format));
  options.push_back(hm_option("FrameRate", 1));
  options.push_back(hm_option("FramesToBeEncoded", 1));

  // HM pads the picture to a multiple of the minimum coding block size and sets the
  // conformance window accordingly.
  options.push_back(hm_option("ConformanceWindowMode", 1));

  const uint32_t min_coding_block_size = 8;
  if (encoder->coding_tool[hm_tool_cabac_bypass_alignment]) {
    // Only the High Throughput 4:4:4 16 Intra profile allows to align the bypass bins.
    // It has no name in HM and is described by its constraints.
    options.push_back(hm_option("Profile", "high-throughput-RExt"));
    options.push_back(hm_option("MaxBitDepthConstraint", 16));
    options.push_back(hm_option("MaxChromaFormatConstraint", 444));
    options.push_back(hm_option("IntraConstraintFlag", 1));
  }
  else {
    options.push_back(hm_option("Profile", hm_profile(encoder, chroma, bit_depth)));
  }
  options.push_back(hm_option("Level", hm_level_for_picture_size((width + min_coding_block_size - 1) / min_coding_block_size * min_coding_block_size,
                                                                 (height + min_coding_block_size - 1) / min_coding_block_size * min_coding_block_size)));
  options.push_back(hm_option("Tier", "main"));

  // --- coding structure: intra pictures only, as the intra profiles demand. HM codes the
  // first picture, which is the only one here, as an IDR picture.

  options.push_back(hm_option("IntraPeriod", 1));
  options.push_back(hm_option("DecodingRefreshType", 1));
  options.push_back(hm_option("GOPSize", 1));

  // --- coding tools, as in cfg/encoder_intra_main10.cfg of HM

  options.push_back(hm_option("MaxCUWidth", 64));
  options.push_back(hm_option("MaxCUHeight", 64));
  options.push_back(hm_option("MaxPartitionDepth", 4));
  options.push_back(hm_option("QuadtreeTULog2MaxSize", 5));
  options.push_back(hm_option("QuadtreeTULog2MinSize", 2));
  options.push_back(hm_option("QuadtreeTUMaxDepthInter", 3));
  options.push_back(hm_option("QuadtreeTUMaxDepthIntra", 3));
  options.push_back(hm_option("RDOQ", 1));
  options.push_back(hm_option("RDOQTS", 1));
  options.push_back(hm_option("SAO", 1));
  options.push_back(hm_option("AMP", 1));
  options.push_back(hm_option("TransformSkip", 1));
  options.push_back(hm_option("TransformSkipFast", 1));
  options.push_back(hm_option("SEIDecodedPictureHash", 0));

  // --- coding tools of the range extensions and of the screen content coding extensions

  for (int i = 0; i < HM_NUM_CODING_TOOLS; i++) {
    const hm_coding_tool& tool = hm_coding_tools[i];

    if (tool.tool_set == hm_tool_set_screen_content_coding && !variant->has_screen_content_coding) {
      // This variant of HM does not know the option.
      continue;
    }

    const bool enable = (encoder->coding_tool[i] != tool.hm_option_is_inverted);
    options.push_back(hm_option(tool.hm_option, enable ? 1 : 0));
  }

  if (variant->has_screen_content_coding) {
    // HM only finds the repetitions that make intra block copy worthwhile with this search.
    options.push_back(hm_option("HashBasedIntraBlockCopySearchEnabled", 1));
  }

  options.push_back(hm_option("TransformSkipLog2MaxSize", encoder->transform_skip_log2_max_size));

  // --- quality

  if (encoder->lossless) {
    options.push_back(hm_option("QP", 0));
    options.push_back(hm_option("CostMode", "lossless"));
    options.push_back(hm_option("TransquantBypassEnable", 1));
    options.push_back(hm_option("CUTransquantBypassFlagForce", 1));
  }
  else {
    options.push_back(hm_option("QP", ((100 - encoder->quality) * 51 + 50) / 100));
  }

  // --- VUI

  heif_color_profile_nclx* nclx = nullptr;
  heif_error err = heif_image_get_nclx_color_profile(image, &nclx);
  if (err.code != heif_error_Ok) {
    nclx = nullptr;
  }

  auto nclx_deleter = std::unique_ptr<heif_color_profile_nclx, void (*)(heif_color_profile_nclx*)>(nclx, heif_nclx_color_profile_free);

  const bool is_color_image = (input_class == heif_image_input_class_normal ||
                               input_class == heif_image_input_class_thumbnail);

  options.push_back(hm_option("VuiParametersPresent", 1));
  options.push_back(hm_option("VideoSignalTypePresent", 1));
  options.push_back(hm_option("VideoFullRange", nclx ? (nclx->full_range_flag ? 1 : 0) : 1));

  // HM does not write a VUI without a bit rate, although we do not use its rate control.
  options.push_back(hm_option("TargetBitrate", 1000000));

  if (nclx && is_color_image) {
    options.push_back(hm_option("ColourDescriptionPresent", 1));
    options.push_back(hm_option("ColourPrimaries", nclx->color_primaries));
    options.push_back(hm_option("TransferCharacteristics", nclx->transfer_characteristics));
    options.push_back(hm_option("MatrixCoefficients", nclx->matrix_coefficients));
  }

  uint32_t aspect_h = 1, aspect_v = 1;
  heif_image_get_pixel_aspect_ratio(image, &aspect_h, &aspect_v);
  if (is_color_image &&
      aspect_h != aspect_v &&
      aspect_h > 0 && aspect_v > 0 &&
      aspect_h <= 0xFFFF && aspect_v <= 0xFFFF) {
    options.push_back(hm_option("AspectRatioInfoPresent", 1));
    options.push_back(hm_option("AspectRatioIdc", 255));
    options.push_back(hm_option("SarWidth", static_cast<int>(aspect_h)));
    options.push_back(hm_option("SarHeight", static_cast<int>(aspect_v)));
  }

  return options;
}


// --- encoding

// The last lines of what HM has printed. When HM gives up, this is its error message.
static std::string hm_last_messages(std::string messages)
{
  while (!messages.empty() && (messages.back() == '\n' || messages.back() == ' ')) {
    messages.pop_back();
  }

  // HM reports every parameter that it does not accept in a line that starts with "Error: "
  // and exits afterwards.

  size_t pos = messages.find("Error: ");
  if (pos == std::string::npos) {
    pos = messages.rfind('\n');
    pos = (pos == std::string::npos ? 0 : pos + 1);
  }

  messages = messages.substr(pos);
  std::replace(messages.begin(), messages.end(), '\n', ' ');

  return messages;
}


static heif_error hm_error(encoder_struct_hm* encoder, heif_suberror_code subcode, const std::string& message)
{
  encoder->error_message = message;

  return heif_error{heif_error_Encoder_plugin_error, subcode, encoder->error_message.c_str()};
}


static heif_error hm_encode_image(void* encoder_raw, const heif_image* image,
                                  heif_image_input_class input_class)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  heif_error input_error = check_encoder_input_image(image, /*supports_monochrome=*/true,
                                                    {8, 9, 10, 11, 12, 13, 14, 15});
  if (input_error.code != heif_error_Ok) {
    if (heif_image_get_bits_per_pixel_range(image, heif_channel_Y) > HM_MAX_BIT_DEPTH) {
      return hm_error(encoder, heif_suberror_Unsupported_bit_depth,
                      "HEVC images with more than 15 bits per sample cannot be written, "
                      "because the 'hvcC' box cannot signal their bit depth.");
    }

    return input_error;
  }

  const heif_chroma chroma = heif_image_get_chroma_format(image);
  if (chroma != heif_chroma_monochrome &&
      chroma != heif_chroma_420 &&
      chroma != heif_chroma_422 &&
      chroma != heif_chroma_444) {
    return hm_error(encoder, heif_suberror_Unsupported_image_type, "Unsupported chroma type");
  }

  const int input_width = heif_image_get_width(image, heif_channel_Y);
  const int input_height = heif_image_get_height(image, heif_channel_Y);
  if (input_width <= 0 || input_height <= 0) {
    return hm_error(encoder, heif_suberror_Invalid_image_size, "Invalid image size");
  }

  // The size of the HEVC picture. It follows from the chroma format of the image, which
  // is what hm_query_encoded_size() assumes unless the image is monochrome.

  uint32_t width = input_width;
  uint32_t height = input_height;

  if (chroma == heif_chroma_monochrome) {
    hm_query_encoded_size(encoder, input_width, input_height, &width, &height);
  }
  else {
    if (chroma == heif_chroma_420 || chroma == heif_chroma_422) {
      width = (width + 1) & ~1U;
    }

    if (chroma == heif_chroma_420) {
      height = (height + 1) & ~1U;
    }
  }

  const std::string unsupported = hm_check_screen_content_coding(encoder, chroma,
                                                                 heif_image_get_bits_per_pixel_range(image, heif_channel_Y));
  if (!unsupported.empty()) {
    return hm_error(encoder, heif_suberror_Invalid_parameter_value, unsupported);
  }

  const hm_variant* variant = hm_select_variant(encoder);
  assert(variant);

  const std::vector<std::string> options = hm_options(encoder, variant, image, input_class, width, height);

  encoder->output_data.clear();

  hm_variant_result result;

  {
    std::lock_guard<std::mutex> lock(hm_mutex);

    result = variant->encode(options, image, encoder->output_data);
  }

  switch (result.status) {
    case hm_variant_result::Status::ok:
      break;
    case hm_variant_result::Status::configuration_refused:
      return hm_error(encoder, heif_suberror_Encoder_initialization,
                      "HM does not accept the configuration. " + hm_last_messages(result.messages));
    case hm_variant_result::Status::encoding_failed:
      return hm_error(encoder, heif_suberror_Encoder_encoding, "HM: " + hm_last_messages(result.messages));
    case hm_variant_result::Status::out_of_memory:
      return heif_error{heif_error_Memory_allocation_error, heif_suberror_Unspecified, kError_out_of_memory};
  }

  if (encoder->output_data.empty()) {
    return hm_error(encoder, heif_suberror_Encoder_encoding, "HM did not return any data");
  }

  return heif_error_ok;
}


static heif_error hm_get_compressed_data(void* encoder_raw, uint8_t** data, int* size,
                                         heif_encoded_data_type* type)
{
  encoder_struct_hm* encoder = (encoder_struct_hm*) encoder_raw;

  if (encoder->output_data.empty()) {
    *data = nullptr;
    *size = 0;

    return heif_error_ok;
  }

  encoder->active_data = std::move(encoder->output_data.front());
  encoder->output_data.pop_front();

  *data = encoder->active_data.data();
  *size = static_cast<int>(encoder->active_data.size());

  return heif_error_ok;
}


// --- image sequences
//
// This plugin encodes images only. libheif does not load plugins with an API version
// below 4, so the functions for sequences exist, and they all report an error.

static heif_error hm_start_sequence_encoding(void* encoder_raw, const heif_image* image,
                                             heif_image_input_class image_class,
                                             uint32_t framerate_num, uint32_t framerate_denom,
                                             const heif_sequence_encoding_options* options)
{
  return heif_error{heif_error_Unsupported_feature, heif_suberror_Unspecified, kError_no_sequences};
}

static heif_error hm_encode_sequence_frame(void* encoder_raw, const heif_image* image, uintptr_t frame_nr)
{
  return heif_error{heif_error_Unsupported_feature, heif_suberror_Unspecified, kError_no_sequences};
}

static heif_error hm_end_sequence_encoding(void* encoder_raw)
{
  return heif_error{heif_error_Unsupported_feature, heif_suberror_Unspecified, kError_no_sequences};
}

static heif_error hm_get_compressed_data2(void* encoder_raw, uint8_t** data, int* size,
                                          uintptr_t* frame_nr, int* is_keyframe, int* more_frame_packets)
{
  *data = nullptr;
  *size = 0;

  return heif_error{heif_error_Unsupported_feature, heif_suberror_Unspecified, kError_no_sequences};
}


static const heif_encoder_plugin encoder_plugin_hm
    {
        /* plugin_api_version */ 4,
        /* compression_format */ heif_compression_HEVC,
        /* id_name */ "hm",
        /* priority */ HM_PLUGIN_PRIORITY,
        /* supports_lossy_compression */ true,
        /* supports_lossless_compression */ true,
        /* get_plugin_name */ hm_plugin_name,
        /* init_plugin */ hm_init_plugin,
        /* cleanup_plugin */ hm_cleanup_plugin,
        /* new_encoder */ hm_new_encoder,
        /* free_encoder */ hm_free_encoder,
        /* set_parameter_quality */ hm_set_parameter_quality,
        /* get_parameter_quality */ hm_get_parameter_quality,
        /* set_parameter_lossless */ hm_set_parameter_lossless,
        /* get_parameter_lossless */ hm_get_parameter_lossless,
        /* set_parameter_logging_level */ hm_set_parameter_logging_level,
        /* get_parameter_logging_level */ hm_get_parameter_logging_level,
        /* list_parameters */ hm_list_parameters,
        /* set_parameter_integer */ hm_set_parameter_integer,
        /* get_parameter_integer */ hm_get_parameter_integer,
        /* set_parameter_boolean */ hm_set_parameter_integer, // boolean also maps to integer function
        /* get_parameter_boolean */ hm_get_parameter_integer, // boolean also maps to integer function
        /* set_parameter_string */ hm_set_parameter_string,
        /* get_parameter_string */ hm_get_parameter_string,
        /* query_input_colorspace */ hm_query_input_colorspace,
        /* encode_image */ hm_encode_image,
        /* get_compressed_data */ hm_get_compressed_data,
        /* query_input_colorspace (v2) */ hm_query_input_colorspace2,
        /* query_encoded_size (v3) */ hm_query_encoded_size,
        /* minimum_required_libheif_version */ LIBHEIF_MAKE_VERSION(1,21,0),
        /* start_sequence_encoding (v4) */ hm_start_sequence_encoding,
        /* encode_sequence_frame (v4) */ hm_encode_sequence_frame,
        /* end_sequence_encoding (v4) */ hm_end_sequence_encoding,
        /* get_compressed_data2 (v4) */ hm_get_compressed_data2,
        /* does_indicate_keyframes (v4) */ 0
    };

const heif_encoder_plugin* get_encoder_plugin_hm()
{
  return &encoder_plugin_hm;
}


#if PLUGIN_HM
heif_plugin_info plugin_info{
    1,
    heif_plugin_type_encoder,
    &encoder_plugin_hm
};
#endif
