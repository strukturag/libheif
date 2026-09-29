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

// How HM is used here
// -------------------
// HM consists of an encoder library and a command line application. The library takes a
// picture in memory and returns NAL units in memory, but it expects several hundred
// parameters that fit to each other, and it does not check them: the checks, and the
// derivation of the dependent values, are part of the application.
//
// So we use two classes of the application as well. TAppEncCfg holds the configuration.
// It reads it from a list of options, which we pass in memory, the same options that
// can be given on the command line of HM. TAppEncTop applies the configuration to the
// encoder. We do not use the parts of the application that read and write files.
//
// HM prints to stdout and calls exit() on errors. hm_as_library.h, which is included into
// all HM source files when HM is built, redirects both: the text is collected in a
// buffer, and exit() throws heif_hm_exit.

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "encoder_hm.h"
#include "encoder_input_check.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// third-party/hm.cmd builds HM with HIGH_BITDEPTH, which changes the data types in the
// HM headers. CMake verifies that the HM build we link to has this setting.
#define RExt__HIGH_BIT_DEPTH_SUPPORT 1

#define LIBHEIF_HM_PLUGIN 1
#include "hm_as_library.h"

// The symbols of HM are hidden, see third-party/hm.cmd.
#if defined(__GNUC__)
#pragma GCC visibility push(hidden)
#endif
#include "TAppEncTop.h"
#if defined(__GNUC__)
#pragma GCC visibility pop
#endif


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

// Coding tools of the HEVC range extensions. They are disabled by default, since not
// every decoder implements all of them.
struct hm_coding_tool
{
  const char* parameter_name;
  const char* hm_option;
  bool default_value;

  // Tools that Table A.2 of ITU-T H.265 only allows in the profiles for 4:4:4.
  bool needs_444_profile;
};

static const hm_coding_tool hm_coding_tools[] = {
  {"cross-component-prediction", "CrossComponentPrediction",      false, true},
  {"implicit-rdpcm",             "ImplicitResidualDPCM",          false, true},
  {"explicit-rdpcm",             "ExplicitResidualDPCM",          false, true},
  {"transform-skip-rotation",    "ResidualRotation",              false, true},
  {"transform-skip-context",     "SingleSignificanceMapContext",  false, true},
  {"persistent-rice-adaptation", "GolombRiceParameterAdaptation", false, true},
  {"intra-smoothing",            "IntraReferenceSmoothing",       true,  true},
  {"extended-precision",         "ExtendedPrecision",             false, true},
  {"cabac-bypass-alignment",     "AlignCABACBeforeBypass",        false, true},
};

static const int HM_NUM_CODING_TOOLS = sizeof(hm_coding_tools) / sizeof(hm_coding_tools[0]);

enum
{
  hm_tool_intra_smoothing = 6,
  hm_tool_extended_precision = 7,
  hm_tool_cabac_bypass_alignment = 8
};

static const char* kParam_transform_skip_max_size = "transform-skip-log2-max-size";


struct encoder_struct_hm
{
  // --- parameters

  int quality = 50;
  bool lossless = false;
  heif_chroma chroma = heif_chroma_420;

  bool coding_tool[HM_NUM_CODING_TOOLS] = {};
  int transform_skip_log2_max_size = 2;

  // --- output

  std::deque<std::vector<uint8_t>> output_data;

  std::vector<uint8_t> active_data; // holds the data that we just returned

  std::string error_message; // holds the message of the error that we just returned
};


// HM keeps part of its state in global variables.
static std::mutex hm_mutex;


#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

static void hm_set_default_parameters(void* encoder);


static const char* hm_plugin_name()
{
  snprintf(plugin_name, MAX_PLUGIN_NAME_LENGTH, "HM HEVC reference encoder %s (experimental)", NV_VERSION);
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
    if (strcmp(name, hm_coding_tools[i].parameter_name) == 0) {
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


// The profile with the tightest constraints that the image fits into. All our pictures
// are intra pictures.
static const char* hm_profile(const encoder_struct_hm* encoder, heif_chroma chroma, int bit_depth)
{
  bool needs_444_profile = false;
  for (int i = 0; i < HM_NUM_CODING_TOOLS; i++) {
    if (hm_coding_tools[i].needs_444_profile &&
        encoder->coding_tool[i] != hm_coding_tools[i].default_value) {
      needs_444_profile = true;
    }
  }

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


static std::vector<std::string> hm_options(const encoder_struct_hm* encoder, const heif_image* image,
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

  // --- coding tools of the range extensions

  for (int i = 0; i < HM_NUM_CODING_TOOLS; i++) {
    options.push_back(hm_option(hm_coding_tools[i].hm_option, encoder->coding_tool[i] ? 1 : 0));
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

namespace {

class HmEncoder : public TAppEncTop
{
public:
  ~HmEncoder()
  {
    if (m_encoder_created) {
      getTEncTop().deletePicBuffer();
      getTEncTop().destroy();
    }

    destroy();
  }

  // Returns false when HM does not accept the options.
  bool configure(const std::vector<std::string>& options)
  {
    std::vector<std::string> arguments;
    arguments.emplace_back("libheif");
    arguments.insert(arguments.end(), options.begin(), options.end());

    std::vector<char*> argv;
    for (std::string& argument : arguments) {
      argv.push_back(argument.data());
    }

    create();

    if (!parseCfg(static_cast<Int>(argv.size()), argv.data())) {
      return false;
    }

    xInitLibCfg();

    getTEncTop().create();
    m_encoder_created = true;

    xInitLib(false);

    return true;
  }


  void encode(const heif_image* image, std::deque<std::vector<uint8_t>>& out_nals)
  {
    TComPicYuv original;
    TComPicYuv true_original;

    original.create(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);
    true_original.create(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);

    static const heif_channel channels[3] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};

    for (UInt c = 0; c < original.getNumberValidComponents(); c++) {
      const ComponentID component = ComponentID(c);

      size_t stride = 0;
      const uint8_t* plane = heif_image_get_plane_readonly2(image, channels[c], &stride);

      copy_plane(original.getAddr(component), original.getStride(component),
                 original.getWidth(component), original.getHeight(component),
                 plane, stride,
                 heif_image_get_width(image, channels[c]), heif_image_get_height(image, channels[c]),
                 heif_image_get_bits_per_pixel_range(image, channels[c]));
    }

    original.copyToPic(&true_original);

    // HM writes the reconstructed picture into a buffer that we have to provide.

    struct Reconstructions
    {
      TComList<TComPicYuv*> list;

      ~Reconstructions()
      {
        for (TComPicYuv* picture : list) {
          picture->destroy();
          delete picture;
        }
      }
    } reconstructions;

    TComPicYuv* reconstruction = new TComPicYuv;
    reconstructions.list.pushBack(reconstruction);
    reconstruction->create(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);

    std::list<AccessUnit> access_units;
    Int num_encoded = 0;

    getTEncTop().encode(true, &original, &true_original,
#if JVET_X0048_X0103_FILM_GRAIN
                        nullptr,
#endif
                        IPCOLOURSPACE_UNCHANGED, IPCOLOURSPACE_UNCHANGED,
                        reconstructions.list, access_units, num_encoded);

    for (const AccessUnit& access_unit : access_units) {
      for (const NALUnitEBSP* nal : access_unit) {
        const std::string data = nal->m_nalUnitData.str();
        out_nals.emplace_back(data.begin(), data.end());
      }
    }
  }

private:
  bool m_encoder_created = false;

  // Copies a plane of the image into the larger plane of the HM picture and fills the
  // rest with copies of the border.
  static void copy_plane(Pel* dst, int dst_stride, int dst_width, int dst_height,
                         const uint8_t* src, size_t src_stride, int src_width, int src_height,
                         int bit_depth)
  {
    src_width = std::min(src_width, dst_width);
    src_height = std::min(src_height, dst_height);

    for (int y = 0; y < dst_height; y++) {
      const uint8_t* src_row = src + std::min(y, src_height - 1) * src_stride;
      Pel* dst_row = dst + y * dst_stride;

      if (bit_depth > 8) {
        for (int x = 0; x < src_width; x++) {
          uint16_t value;
          memcpy(&value, src_row + 2 * x, 2);
          dst_row[x] = static_cast<Pel>(value);
        }
      }
      else {
        for (int x = 0; x < src_width; x++) {
          dst_row[x] = static_cast<Pel>(src_row[x]);
        }
      }

      for (int x = src_width; x < dst_width; x++) {
        dst_row[x] = dst_row[src_width - 1];
      }
    }
  }
};

} // namespace


// The last lines of what HM has printed. When HM gives up, this is its error message.
static std::string hm_last_messages()
{
  std::string messages = heif_hm_messages();

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

  const std::vector<std::string> options = hm_options(encoder, image, input_class, width, height);

  encoder->output_data.clear();

  std::lock_guard<std::mutex> lock(hm_mutex);

  heif_hm_messages().clear();

  try {
    HmEncoder hm;

    if (!hm.configure(options)) {
      return hm_error(encoder, heif_suberror_Encoder_initialization,
                      "HM does not accept the configuration. " + hm_last_messages());
    }

    hm.encode(image, encoder->output_data);
  }
  catch (const heif_hm_exit&) {
    encoder->output_data.clear();
    return hm_error(encoder, heif_suberror_Encoder_encoding, "HM: " + hm_last_messages());
  }
  catch (const std::bad_alloc&) {
    encoder->output_data.clear();
    return heif_error{heif_error_Memory_allocation_error, heif_suberror_Unspecified, kError_out_of_memory};
  }
  catch (const std::exception& e) {
    encoder->output_data.clear();
    return hm_error(encoder, heif_suberror_Encoder_encoding, std::string("HM: ") + e.what());
  }

  heif_hm_messages().clear();

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
