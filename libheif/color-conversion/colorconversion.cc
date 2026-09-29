/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
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


#include "colorconversion.h"
#include "common_utils.h"
#include "nclx.h"
#include <typeinfo>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <iostream>
#include <set>
#include <cmath>
#include <limits>
#include <string>
#include "rgb2yuv.h"
#include "rgb2yuv_sharp.h"
#include "yuv2rgb.h"
#include "rgb2rgb.h"
#include "monochrome.h"
#include "alpha.h"
#include "hdr_sdr.h"
#include "chroma_sampling.h"
#include "bayer_bilinear.h"

#if ENABLE_MULTITHREADING_SUPPORT

#include <mutex>

#endif

#define DEBUG_ME 0
#define DEBUG_PIPELINE_CREATION 0

#define USE_CENTER_CHROMA_422 0


std::ostream& operator<<(std::ostream& ostr, heif_colorspace c)
{
  switch (c) {
    case heif_colorspace_RGB:
      ostr << "RGB";
      break;
    case heif_colorspace_YCbCr:
      ostr << "YCbCr";
      break;
    case heif_colorspace_monochrome:
      ostr << "mono";
      break;
    case heif_colorspace_undefined:
      ostr << "undefined";
      break;
    case heif_colorspace_filter_array:
      ostr << "filter_array";
      break;
    default:
      assert(false);
  }

  return ostr;
}

std::ostream& operator<<(std::ostream& ostr, heif_chroma c)
{
  switch (c) {
    case heif_chroma_420:
      ostr << "420";
      break;
    case heif_chroma_422:
      ostr << "422";
      break;
    case heif_chroma_444:
      ostr << "444";
      break;
    case heif_chroma_monochrome:
      ostr << "mono";
      break;
    case heif_chroma_interleaved_RGB:
      ostr << "RGB";
      break;
    case heif_chroma_interleaved_RGBA:
      ostr << "RGBA";
      break;
    case heif_chroma_interleaved_RRGGBB_BE:
      ostr << "RRGGBB_BE";
      break;
    case heif_chroma_interleaved_RRGGBB_LE:
      ostr << "RRGGBBB_LE";
      break;
    case heif_chroma_interleaved_RRGGBBAA_BE:
      ostr << "RRGGBBAA_BE";
      break;
    case heif_chroma_interleaved_RRGGBBAA_LE:
      ostr << "RRGGBBBAA_LE";
      break;
    case heif_chroma_undefined:
      ostr << "undefined";
      break;
    default:
      assert(false);
  }

  return ostr;
}

#if DEBUG_ME

static void __attribute__ ((unused)) print_spec(std::ostream& ostr, const std::shared_ptr<HeifPixelImage>& img)
{
  ostr << "colorspace=" << img->get_colorspace()
       << " chroma=" << img->get_chroma_format();

  if (img->get_colorspace() == heif_colorspace_RGB) {
    if (img->get_chroma_format() == heif_chroma_444) {
      ostr << " bpp(R)=" << ((int) img->get_bits_per_pixel(heif_channel_R));
    }
    else {
      ostr << " bpp(interleaved)=" << ((int) img->get_bits_per_pixel(heif_channel_interleaved));
    }
  }
  else if (img->get_colorspace() == heif_colorspace_YCbCr ||
           img->get_colorspace() == heif_colorspace_monochrome) {
    ostr << " bpp(Y)=" << ((int) img->get_bits_per_pixel(heif_channel_Y));
  }

  ostr << "\n";
}

#endif


ColorState::ColorState(heif_colorspace cs, heif_chroma chr, bool with_alpha, int bpp)
    : colorspace(cs), chroma(chr)
{
  set_color_bits_per_pixel(bpp);
  bits_per_pixel_alpha = with_alpha ? bpp : 0;
}


int ColorState::get_bits_per_pixel(heif_channel channel) const
{
  switch (channel) {
    case heif_channel_R:
      return bits_per_pixel_R;
    case heif_channel_G:
      return bits_per_pixel_G;
    case heif_channel_B:
      return bits_per_pixel_B;
    case heif_channel_Y:
      return bits_per_pixel_Y;
    case heif_channel_Cb:
      return bits_per_pixel_Cb;
    case heif_channel_Cr:
      return bits_per_pixel_Cr;
    case heif_channel_Alpha:
      return bits_per_pixel_alpha;
    case heif_channel_filter_array:
      return bits_per_pixel_filter_array;
    case heif_channel_interleaved:
      return bits_per_pixel_R;
    default:
      return 0;
  }
}


void ColorState::set_bits_per_pixel(heif_channel channel, int bpp)
{
  switch (channel) {
    case heif_channel_R:
      bits_per_pixel_R = bpp;
      break;
    case heif_channel_G:
      bits_per_pixel_G = bpp;
      break;
    case heif_channel_B:
      bits_per_pixel_B = bpp;
      break;
    case heif_channel_Y:
      bits_per_pixel_Y = bpp;
      break;
    case heif_channel_Cb:
      bits_per_pixel_Cb = bpp;
      break;
    case heif_channel_Cr:
      bits_per_pixel_Cr = bpp;
      break;
    case heif_channel_Alpha:
      bits_per_pixel_alpha = bpp;
      break;
    case heif_channel_filter_array:
      bits_per_pixel_filter_array = bpp;
      break;
    case heif_channel_interleaved:
      bits_per_pixel_R = bits_per_pixel_G = bits_per_pixel_B = bpp;
      break;
    default:
      break;
  }
}


void ColorState::set_color_bits_per_pixel(int bpp)
{
  bits_per_pixel_R = bits_per_pixel_G = bits_per_pixel_B = 0;
  bits_per_pixel_Y = bits_per_pixel_Cb = bits_per_pixel_Cr = 0;
  bits_per_pixel_filter_array = 0;

  if (colorspace == heif_colorspace_filter_array) {
    bits_per_pixel_filter_array = bpp;
    return;
  }

  switch (chroma) {
    case heif_chroma_planar: // == heif_chroma_monochrome
      if (colorspace == heif_colorspace_RGB) {
        bits_per_pixel_R = bits_per_pixel_G = bits_per_pixel_B = bpp;
      }
      else {
        bits_per_pixel_Y = bpp;
      }
      break;

    case heif_chroma_420:
    case heif_chroma_422:
      bits_per_pixel_Y = bits_per_pixel_Cb = bits_per_pixel_Cr = bpp;
      break;

    case heif_chroma_444:
      if (colorspace == heif_colorspace_RGB) {
        bits_per_pixel_R = bits_per_pixel_G = bits_per_pixel_B = bpp;
      }
      else {
        bits_per_pixel_Y = bits_per_pixel_Cb = bits_per_pixel_Cr = bpp;
      }
      break;

    case heif_chroma_interleaved_RGB:
    case heif_chroma_interleaved_RGBA:
    case heif_chroma_interleaved_RRGGBB_BE:
    case heif_chroma_interleaved_RRGGBB_LE:
    case heif_chroma_interleaved_RRGGBBAA_BE:
    case heif_chroma_interleaved_RRGGBBAA_LE:
      bits_per_pixel_R = bits_per_pixel_G = bits_per_pixel_B = bpp;
      break;

    default:
      break;
  }
}


int ColorState::get_uniform_color_bits_per_pixel() const
{
  int uniform = 0;

  for (int bpp : {bits_per_pixel_R, bits_per_pixel_G, bits_per_pixel_B,
                  bits_per_pixel_Y, bits_per_pixel_Cb, bits_per_pixel_Cr,
                  bits_per_pixel_filter_array}) {
    if (bpp == 0) {
      continue; // plane does not exist
    }

    if (uniform == 0) {
      uniform = bpp;
    }
    else if (bpp != uniform) {
      return 0;
    }
  }

  return uniform;
}


int ColorState::get_uniform_bits_per_pixel() const
{
  int uniform = get_uniform_color_bits_per_pixel();

  if (uniform != 0 && bits_per_pixel_alpha != 0 && bits_per_pixel_alpha != uniform) {
    return 0;
  }

  return uniform;
}


int ColorState::get_max_color_bits_per_pixel() const
{
  return std::max({bits_per_pixel_R, bits_per_pixel_G, bits_per_pixel_B,
                   bits_per_pixel_Y, bits_per_pixel_Cb, bits_per_pixel_Cr,
                   bits_per_pixel_filter_array});
}


int ColorState::get_max_bits_per_pixel() const
{
  return std::max({bits_per_pixel_R, bits_per_pixel_G, bits_per_pixel_B,
                   bits_per_pixel_Y, bits_per_pixel_Cb, bits_per_pixel_Cr,
                   bits_per_pixel_alpha, bits_per_pixel_filter_array});
}


// Applies 'pred' to the depth of every existing plane (planes with depth 0 do not exist and
// are skipped) and returns whether it holds for all of them.
template<typename Pred>
static bool all_existing_planes_satisfy(const ColorState& s, bool include_alpha, Pred pred)
{
  for (int bpp : {s.bits_per_pixel_R, s.bits_per_pixel_G, s.bits_per_pixel_B,
                  s.bits_per_pixel_Y, s.bits_per_pixel_Cb, s.bits_per_pixel_Cr,
                  s.bits_per_pixel_filter_array}) {
    if (bpp != 0 && !pred(bpp)) {
      return false;
    }
  }

  if (include_alpha && s.bits_per_pixel_alpha != 0 && !pred(s.bits_per_pixel_alpha)) {
    return false;
  }

  return true;
}


int ColorState::get_bytes_per_sample(heif_channel channel) const
{
  return bytes_per_sample_for_bit_depth(get_bits_per_pixel(channel)); // 0 if the plane does not exist
}


int ColorState::get_max_bytes_per_sample() const
{
  // The bit depth to sample width mapping is monotonic, so the widest plane is the deepest.
  return bytes_per_sample_for_bit_depth(get_max_bits_per_pixel());
}


bool ColorState::color_channels_have_bytes_per_sample(int bytes) const
{
  return all_existing_planes_satisfy(*this, false, [bytes](int bpp) {
    return bytes_per_sample_for_bit_depth(bpp) == bytes;
  });
}


bool ColorState::all_channels_have_bytes_per_sample(int bytes) const
{
  return all_existing_planes_satisfy(*this, true, [bytes](int bpp) {
    return bytes_per_sample_for_bit_depth(bpp) == bytes;
  });
}


bool ColorState::operator==(const ColorState& b) const
{
  bool mainParamsMatch = (colorspace == b.colorspace &&
                          chroma == b.chroma &&
                          bits_per_pixel_R == b.bits_per_pixel_R &&
                          bits_per_pixel_G == b.bits_per_pixel_G &&
                          bits_per_pixel_B == b.bits_per_pixel_B &&
                          bits_per_pixel_Y == b.bits_per_pixel_Y &&
                          bits_per_pixel_Cb == b.bits_per_pixel_Cb &&
                          bits_per_pixel_Cr == b.bits_per_pixel_Cr &&
                          bits_per_pixel_alpha == b.bits_per_pixel_alpha &&
                          bits_per_pixel_filter_array == b.bits_per_pixel_filter_array);

  if (!mainParamsMatch) {
    return false;
  }

  if (colorspace == heif_colorspace_YCbCr) {
    bool ycbcr_parameters_match = nclx.equal_except_transfer_curve(b.nclx);

    if (!ycbcr_parameters_match) {
      return false;
    }
  }

  return true;
}


struct Node
{
  Node() = default;

  Node(int prev,
       const std::shared_ptr<ColorConversionOperation>& _op,
      //const ColorState& _input_state,
       const ColorState& _output_state,
       int _speed_cost)
  {
    prev_processed_idx = prev;
    op = _op;
    //input_state = _input_state;
    output_state = _output_state;
    speed_costs = _speed_cost;
  }

  int prev_processed_idx = -1;
  std::shared_ptr<ColorConversionOperation> op;
  //ColorState input_state;
  ColorState output_state;
  int speed_costs;
};

std::ostream& operator<<(std::ostream& ostr, const ColorState& state)
{
  ostr << "colorspace=" << state.colorspace << " chroma=" << state.chroma;

  auto print_plane = [&ostr](const char* name, int bpp) {
    if (bpp != 0) {
      ostr << " bpp(" << name << ")=" << bpp;
    }
  };

  print_plane("Y", state.bits_per_pixel_Y);
  print_plane("Cb", state.bits_per_pixel_Cb);
  print_plane("Cr", state.bits_per_pixel_Cr);
  print_plane("R", state.bits_per_pixel_R);
  print_plane("G", state.bits_per_pixel_G);
  print_plane("B", state.bits_per_pixel_B);
  print_plane("filter_array", state.bits_per_pixel_filter_array);

  if (state.has_alpha()) {
    ostr << " alpha_bpp=" << state.bits_per_pixel_alpha;
  }
  else {
    ostr << " alpha=no";
  }

  if (state.colorspace == heif_colorspace_YCbCr) {
    ostr << " matrix-coefficients=" << state.nclx.get_matrix_coefficients()
         << " colour-primaries=" << state.nclx.get_colour_primaries()
         << " transfer-characteristics=" << state.nclx.get_transfer_characteristics()
         << " full-range=" << (state.nclx.get_full_range_flag() ? "yes" : "no");
  }

  return ostr;
}

std::vector<std::shared_ptr<ColorConversionOperation>> ColorConversionPipeline::m_operation_pool;

void ColorConversionPipeline::init_ops()
{
#if ENABLE_MULTITHREADING_SUPPORT
  static std::mutex init_ops_mutex;
  std::lock_guard<std::mutex> lock(init_ops_mutex);
#endif
  if (!m_operation_pool.empty()) {
    return;
  }

  std::vector<std::shared_ptr<ColorConversionOperation>>& ops = m_operation_pool;
  ops.emplace_back(std::make_shared<Op_RGB_to_RGB24_32>());
  ops.emplace_back(std::make_shared<Op_RGB24_32_to_RGB>());
  ops.emplace_back(std::make_shared<Op_YCbCr_to_RGB<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr_to_RGB<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr420_to_RGB24>());
  ops.emplace_back(std::make_shared<Op_YCbCr420_to_RGB32>());
  ops.emplace_back(std::make_shared<Op_YCbCr420_to_RRGGBBaa>());
  ops.emplace_back(std::make_shared<Op_RGB_HDR_to_RRGGBBaa_BE>());
  ops.emplace_back(std::make_shared<Op_RGB_to_RRGGBBaa_BE>());
  ops.emplace_back(std::make_shared<Op_mono_to_YCbCr420>());
  ops.emplace_back(std::make_shared<Op_mono_to_RGB24_32>());
  ops.emplace_back(std::make_shared<Op_bayer_bilinear_to_RGB24_32>());
  ops.emplace_back(std::make_shared<Op_RRGGBBaa_swap_endianness>());
  ops.emplace_back(std::make_shared<Op_RRGGBBaa_BE_to_RGB_HDR>());
  ops.emplace_back(std::make_shared<Op_RGB24_32_to_YCbCr>());
  ops.emplace_back(std::make_shared<Op_RGB_to_YCbCr<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_RGB_to_YCbCr<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_RRGGBBxx_HDR_to_YCbCr420>());
  ops.emplace_back(std::make_shared<Op_RGB24_32_to_YCbCr444_GBR>());
  ops.emplace_back(std::make_shared<Op_drop_alpha_plane>());
  ops.emplace_back(std::make_shared<Op_flatten_alpha_plane<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_flatten_alpha_plane<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_adjust_alpha_bit_depth>());
  ops.emplace_back(std::make_shared<Op_to_hdr_planes>());
  ops.emplace_back(std::make_shared<Op_to_sdr_planes>());
  ops.emplace_back(std::make_shared<Op_YCbCr420_bilinear_to_YCbCr444<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr420_bilinear_to_YCbCr444<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr422_bilinear_to_YCbCr444<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr422_bilinear_to_YCbCr444<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr444_to_YCbCr420_average<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr444_to_YCbCr420_average<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr444_to_YCbCr422_average<uint8_t>>());
  ops.emplace_back(std::make_shared<Op_YCbCr444_to_YCbCr422_average<uint16_t>>());
  ops.emplace_back(std::make_shared<Op_Any_RGB_to_YCbCr_420_Sharp>());
}


void ColorConversionPipeline::release_ops()
{
  m_operation_pool.clear();
}


bool ColorConversionPipeline::construct_pipeline(const ColorState& input_state,
                                                 const ColorState& target_state,
                                                 const heif_color_conversion_options& options,
                                                 const heif_color_conversion_options_ext& options_ext)
{
  m_conversion_steps.clear();

  m_options = options;
  m_options_ext = options_ext;

  if (input_state == target_state) {
    return true;
  }

#if DEBUG_ME
  std::cerr << "--- construct_pipeline\n";
  std::cerr << "from: " << input_state << "\nto: " << target_state << "\n";
#endif

  init_ops(); // to be sure these are initialized even without heif_init()

  std::vector<std::shared_ptr<ColorConversionOperation>>& ops = m_operation_pool;

  // --- Dijkstra search for the minimum-cost conversion pipeline

  std::vector<Node> processed_states;
  std::vector<Node> border_states;
  border_states.emplace_back(-1, nullptr, input_state, 0);

  while (!border_states.empty()) {
    int minIdx = -1;
    int minCost = std::numeric_limits<int>::max();
    for (int i = 0; i < (int) border_states.size(); i++) {
      int cost = border_states[i].speed_costs;
      if (cost < minCost) {
        minIdx = i;
        minCost = cost;
      }
    }

    assert(minIdx >= 0);


    // move minimum-cost border_state into processed_states

    processed_states.push_back(border_states[minIdx]);

    border_states[minIdx] = border_states.back();
    border_states.pop_back();

#if DEBUG_PIPELINE_CREATION
    std::cerr << "- expand node: " << processed_states.back().output_state
        << " with cost " << processed_states.back().speed_costs << " \n";
#endif

    if (processed_states.back().output_state == target_state) {
      // end-state found, backtrack path to find conversion pipeline

      size_t idx = processed_states.size() - 1;
      int len = 0;
      while (idx > 0) {
        idx = processed_states[idx].prev_processed_idx;
        len++;
      }

      m_conversion_steps.resize(len);

      idx = processed_states.size() - 1;
      int step = 0;
      while (idx > 0) {
        m_conversion_steps[len - 1 - step].operation = processed_states[idx].op;
        m_conversion_steps[len - 1 - step].output_state = processed_states[idx].output_state;
        if (step > 0) {
          m_conversion_steps[len - step].input_state = m_conversion_steps[len - 1 - step].output_state;
        }

        //printf("cost: %f\n",processed_states[idx].color_state.costs.total(options.criterion));
        idx = processed_states[idx].prev_processed_idx;
        step++;
      }

      m_conversion_steps[0].input_state = input_state;

      assert(m_conversion_steps.back().output_state == target_state);

#if DEBUG_ME
      std::cerr << debug_dump_pipeline();
#endif

      return true;
    }


    // expand the node with minimum cost

    for (const auto& op_ptr : ops) {

#if DEBUG_PIPELINE_CREATION
      auto& op = *op_ptr;
      std::cerr << "-- apply op: " << typeid(op).name() << "\n";
#endif

      auto out_states = op_ptr->state_after_conversion(processed_states.back().output_state,
                                                       target_state,
                                                       options, options_ext);
      for (const auto& out_state : out_states) {
        int new_op_costs = out_state.speed_costs + processed_states.back().speed_costs;
#if DEBUG_PIPELINE_CREATION
        std::cerr << "--- " << out_state.color_state << " with cost " << new_op_costs << "\n";
#endif

        bool state_exists = false;
        for (const auto& s : processed_states) {
          if (s.output_state == out_state.color_state) {
            state_exists = true;
            break;
          }
        }

        if (!state_exists) {
          for (auto& s : border_states) {
            if (s.output_state == out_state.color_state) {
              state_exists = true;

              // if we reached the same border node with a lower cost, replace the border node

              if (s.speed_costs > new_op_costs) {
                s = {(int) (processed_states.size() - 1),
                     op_ptr,
                     out_state.color_state,
                     out_state.speed_costs};

                s.speed_costs = new_op_costs;
              }
              break;
            }
          }
        }


        // enter the new output state into the list of border states

        if (!state_exists) {
          ColorStateWithCost s = out_state;
          s.speed_costs = s.speed_costs + processed_states.back().speed_costs;

          border_states.emplace_back((int) (processed_states.size() - 1),
                                     op_ptr,
                                     s.color_state,
                                     s.speed_costs);
        }
      }
    }
  }

  return false;
}


std::string ColorConversionPipeline::debug_dump_pipeline() const
{
  std::ostringstream ostr;
  ostr << "final pipeline has " << m_conversion_steps.size() << " steps:\n";
  for (const auto& step : m_conversion_steps) {
    auto& op = *step.operation;
    ostr << "> " << typeid(op).name() << "\n";
  }
  return ostr.str();
}


Result<std::shared_ptr<HeifPixelImage>> ColorConversionPipeline::convert_image(const std::shared_ptr<HeifPixelImage>& input,
                                                                               const heif_security_limits* limits)
{
  std::shared_ptr<HeifPixelImage> in = input;
  std::shared_ptr<HeifPixelImage> out = in;

  for (const auto& step : m_conversion_steps) {

#if DEBUG_ME
    std::cerr << "input spec: ";
    print_spec(std::cerr, in);
#endif

    auto outResult = step.operation->convert_colorspace(in, step.input_state, step.output_state, m_options, m_options_ext, limits);
    if (!outResult) {
      return outResult.error();
    }
    else {
      out = *outResult;
    }

    // copy metadata over to new image
    out->copy_metadata_from(*in);

    // overwrite color profile nclx from color conversion
    out->set_color_profile_nclx(step.output_state.nclx);


    const auto& warnings = in->get_warnings();
    for (const auto& warning : warnings) {
      out->add_warning(warning);
    }

    in = out;
  }

  return out;
}


Result<std::shared_ptr<HeifPixelImage>> convert_colorspace(const std::shared_ptr<HeifPixelImage>& input,
                                                           heif_colorspace target_colorspace,
                                                           heif_chroma target_chroma,
                                                           const nclx_profile& target_profile,
                                                           int output_bpp,
                                                           const heif_color_conversion_options& options,
                                                           const heif_color_conversion_options_ext* options_ext_optional,
                                                           const heif_security_limits* limits)
{
  std::unique_ptr<heif_color_conversion_options_ext, void(*)(heif_color_conversion_options_ext*)>
      options_ext(heif_color_conversion_options_ext_alloc(), heif_color_conversion_options_ext_free);

  heif_color_conversion_options_ext_copy(options_ext.get(), options_ext_optional);


  // --- check that input image is valid

  uint32_t width = input->get_width();
  uint32_t height = input->get_height();

  // alpha image should have full image resolution

  if (input->has_channel(heif_channel_Alpha)) {
    if (input->get_width(heif_channel_Alpha) != width ||
        input->get_height(heif_channel_Alpha) != height) {
      return Error::InternalError;
    }
  }

  // check for valid target YCbCr chroma formats

  if (target_colorspace == heif_colorspace_YCbCr) {
    if (target_chroma != heif_chroma_420 &&
        target_chroma != heif_chroma_422 &&
        target_chroma != heif_chroma_444) {
      return Error::InternalError;
    }
  }

  // --- prepare conversion

  // The operators read the planes that the colorspace and chroma format imply, with the sizes
  // they imply. Refuse anything else up front instead of letting an operator run into a missing,
  // duplicate or undersized plane: a plane set that does not match the format, or a colorspace
  // without a defined layout (custom multi-component data has nothing to convert). Planes with
  // channel heif_channel_unknown (the padding components of 'unci') are tolerated; they belong
  // to no colour model and are not carried into the output.
  if (Error err = input->check_plane_layout()) {
    return Error{heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_image_type,
                 "Color conversion: " + err.message};
  }

  ColorState input_state;
  input_state.colorspace = input->get_colorspace();
  input_state.chroma = input->get_chroma_format();
  if (input->has_nclx_color_profile()) {
    input_state.nclx = input->get_color_profile_nclx();
  }

  input_state.nclx.replace_undefined_values_with_sRGB_defaults();

  std::set<enum heif_channel> channels = input->get_channel_set();
  assert(!channels.empty());

  // Record the bit depth of every plane the image has. They may differ from each other
  // (e.g. 'unci' declares a depth per component), which is why ColorState keeps one value
  // per plane instead of a single image-wide depth.
  for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr,
                               heif_channel_R, heif_channel_G, heif_channel_B,
                               heif_channel_Alpha, heif_channel_filter_array}) {
    if (input->has_channel(channel)) {
      input_state.set_bits_per_pixel(channel, input->get_bits_per_pixel(channel));
    }
  }

  // Interleaved RGB formats keep all components in one plane. Represent them by their
  // per-component depth so that operators see the same R/G/B (and alpha) fields as for
  // planar RGB.
  if (input->has_channel(heif_channel_interleaved)) {
    int bpp = input->get_bits_per_pixel(heif_channel_interleaved);
    input_state.set_bits_per_pixel(heif_channel_interleaved, bpp);
    if (is_interleaved_with_alpha(input->get_chroma_format())) {
      input_state.bits_per_pixel_alpha = bpp;
    }
  }

  ColorState output_state = input_state;
  output_state.colorspace = target_colorspace;
  output_state.chroma = target_chroma;
  output_state.nclx = target_profile;

  // If some output nclx values are unspecified, set them to the same as the input.

  if (output_state.nclx.get_matrix_coefficients() == heif_matrix_coefficients_unspecified) {
    output_state.nclx.set_matrix_coefficients(input_state.nclx.get_matrix_coefficients());
  }

  if (output_state.nclx.get_colour_primaries() == heif_color_primaries_unspecified) {
    output_state.nclx.set_colour_primaries(input_state.nclx.get_colour_primaries());
  }

  if (output_state.nclx.get_transfer_characteristics() == heif_transfer_characteristic_unspecified) {
    output_state.nclx.set_transfer_characteristics(input_state.nclx.get_transfer_characteristics());
  }

  // If we convert to an interleaved format, we want alpha only if present in the
  // interleaved output format.
  // For planar formats, we include an alpha plane when included in the input.

  bool output_has_alpha;

  if (num_interleaved_components_per_plane(target_chroma) > 1) {
    output_has_alpha = is_interleaved_with_alpha(target_chroma);
  }
  else {
    if (options_ext->alpha_composition_mode != heif_alpha_composition_mode_none) {
      output_has_alpha = false;
    }
    else {
      output_has_alpha = input_state.has_alpha();
    }
  }

  // --- output colour bit depth (0 = keep the input depth)

  int output_color_bpp = output_bpp;

  // interleaved RGB formats always have to be 8-bit

  if (target_chroma == heif_chroma_interleaved_RGB ||
      target_chroma == heif_chroma_interleaved_RGBA) {
    output_color_bpp = 8;
  }

  // The depth that the input's colour planes share, or 0 if they differ ('unci' declares a
  // depth per component). Where a single input depth is needed below and the planes do not
  // agree, the widest plane is used: that is the lossless choice, and a conversion that cannot
  // widen the narrower planes to it is declined by the operators, instead of one plane's depth
  // being picked silently.
  int uniform_input_bpp = input_state.get_uniform_color_bits_per_pixel();
  int input_color_bpp = (uniform_input_bpp != 0) ? uniform_input_bpp : input_state.get_max_color_bits_per_pixel();

  // interleaved RRGGBB formats have to be >8-bit.
  // If we don't know a target bit-depth, use 10 bit.

  if ((target_chroma == heif_chroma_interleaved_RRGGBB_LE ||
       target_chroma == heif_chroma_interleaved_RRGGBB_BE ||
       target_chroma == heif_chroma_interleaved_RRGGBBAA_LE ||
       target_chroma == heif_chroma_interleaved_RRGGBBAA_BE) &&
      (output_color_bpp != 0 ? output_color_bpp : input_color_bpp) <= 8) {
    output_color_bpp = 10;
  }

  bool same_plane_layout = (target_colorspace == input_state.colorspace &&
                            target_chroma == input_state.chroma);

  int output_alpha_bpp;

  if (output_color_bpp == 0 && same_plane_layout) {
    // No depth change requested and the plane layout stays the same: keep the colour planes
    // exactly as they are, even when their depths differ from each other. The alpha plane is
    // brought to the colour depth when there is one (the operators expect them to agree);
    // when the colour planes differ, it is kept as it is, too.
    output_alpha_bpp = (uniform_input_bpp != 0) ? uniform_input_bpp : input_state.bits_per_pixel_alpha;
  }
  else {
    if (output_color_bpp == 0) {
      output_color_bpp = input_color_bpp;
    }

    output_state.set_color_bits_per_pixel(output_color_bpp);

    // Output alpha should always match the output color BPP
    output_alpha_bpp = output_color_bpp;
  }

  output_state.bits_per_pixel_alpha = output_has_alpha ? output_alpha_bpp : 0;

  ColorConversionPipeline pipeline;
  bool success = pipeline.construct_pipeline(input_state, output_state, options, *options_ext);

  if (success && pipeline.is_nop()) {
    return input;
  }

  {
    // The check below also runs when no pipeline could be built, so that the caller gets
    // the specific reason (a plane wider than 16 bits) instead of the generic "unsupported
    // color conversion" error.
    //
    // Every color-conversion operator is written for 8-bit or 16-bit integer samples.
    // They access the planes through uint8_t* / uint16_t* and derive shift amounts and
    // midpoint values from the bit depth (e.g. '128 << (bpp - 8)' in Op_mono_to_YCbCr420).
    // An 'unci' component may however declare a bit depth of up to 256 bits, of which we
    // accept up to 128 (64-bit integers, 32/64-bit floats, complex numbers). Those are
    // stored by HeifPixelImage so that they can be read through the component API. A
    // 64-bit monochrome component reached Op_mono_to_YCbCr420 and shifted an 'int' by
    // 56 (OSS-Fuzz 5154611212910592). A nop conversion is handled above and still hands
    // the image through untouched, so wide components stay accessible to the caller.
    //
    // This is a backstop, not the primary defence. The constraint belongs in each
    // operator's state_after_conversion(), and every operator declares there which
    // sample width it can read (ColorState::color_channels_have_bytes_per_sample() and
    // friends, derived from the same bit depth to storage width mapping that
    // HeifPixelImage uses), so construct_pipeline() above already fails for a wider
    // input and a real conversion never reaches this loop. Keep it until an operator
    // actually supports more than 16 bits per component, then remove it.
    //
    // Planes of differing depth within one image (GHSA-w7mc-p8jc-p853 read 8-bit chroma
    // planes with the 16-bit luma sample width) are handled the same way: each operator
    // declares in state_after_conversion() whether it needs the colour planes to share
    // one depth (ColorState::color_channels_have_same_bpp()), so no pipeline is built
    // through an operator that cannot handle the image, while Op_to_sdr_planes, which
    // lowers every plane on its own, can still equalize such an image to 8 bits.

    for (heif_channel channel : channels) {
      if (input->get_bits_per_pixel(channel) > 16) {
        return Error{heif_error_Unsupported_feature,
                     heif_suberror_Unsupported_bit_depth,
                     "Color conversion of images with more than 16 bits per component is not supported."};
      }
    }
  }

  if (!success) {
    return Error{heif_error_Unsupported_feature,
                 heif_suberror_Unsupported_color_conversion};
  }

  return pipeline.convert_image(input, limits);
}


Result<std::shared_ptr<const HeifPixelImage>> convert_colorspace(const std::shared_ptr<const HeifPixelImage>& input,
                                                                 heif_colorspace colorspace,
                                                                 heif_chroma chroma,
                                                                 const nclx_profile& target_profile,
                                                                 int output_bpp,
                                                                 const heif_color_conversion_options& options,
                                                                 const heif_color_conversion_options_ext* options_ext,
                                                                 const heif_security_limits* limits)
{
  std::shared_ptr<HeifPixelImage> non_const_input = std::const_pointer_cast<HeifPixelImage>(input);

  auto result = convert_colorspace(non_const_input, colorspace, chroma, target_profile, output_bpp, options, options_ext, limits);
  if (!result) {
    return result.error();
  }
  else {
    // TODO: can we simplify this? It's a bit awkward to do these assignments just to get a "const HeifPixelImage".
    std::shared_ptr<const HeifPixelImage> constImage = *result;
    return constImage;
  }
}
