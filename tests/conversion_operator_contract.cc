/*
  libheif unit tests

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

// A color conversion operator declares in state_after_conversion() which image it is going
// to return, and the conversion pipeline is planned with these declarations: the next
// operator reads the planes that its input state lists, with the bit depths given there.
// An operator that returns an image with other planes than it declared makes the next
// operator read planes that do not exist or that have another sample size.
//
// Op_RGB_to_YCbCr copied the chroma format of the target state into the state it declared
// without checking that it is a chroma format of planar YCbCr. For a monochrome target it
// declared a YCbCr state with a Y plane only and returned an image with Y, Cb and Cr planes.
// For an interleaved RGB target it declared a YCbCr state with R, G and B.
//
// The tests run every operator against every state that an image can have and against a
// large set of target states, and compare each returned image with the declaration. They
// also check the place where the pipeline now verifies this for every conversion step.

#include "catch_amalgamated.hpp"
#include "libheif/heif.h"
#include "api_structs.h"
#include "color-conversion/colorconversion.h"
#include "color-conversion/rgb2yuv.h"
#include "image/pixelimage.h"
#include "nclx.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace {

using OptionsExt = std::unique_ptr<heif_color_conversion_options_ext, void (*)(heif_color_conversion_options_ext*)>;

OptionsExt make_options_ext(heif_alpha_composition_mode mode)
{
  OptionsExt ext(heif_color_conversion_options_ext_alloc(), heif_color_conversion_options_ext_free);
  ext->alpha_composition_mode = mode;
  ext->checkerboard_square_size = 4;
  return ext;
}


const heif_chroma kChromas[] = {heif_chroma_monochrome, heif_chroma_420, heif_chroma_422, heif_chroma_444,
                                heif_chroma_interleaved_RGB, heif_chroma_interleaved_RGBA,
                                heif_chroma_interleaved_RRGGBB_BE, heif_chroma_interleaved_RRGGBBAA_BE,
                                heif_chroma_interleaved_RRGGBB_LE, heif_chroma_interleaved_RRGGBBAA_LE};

// Bit depths below 8, with 8 bits, with 16-bit samples and with samples that no operator reads.
const int kDepths[] = {4, 8, 10, 16, 32};


bool is_interleaved(heif_chroma chroma)
{
  return num_interleaved_components_per_plane(chroma) > 1;
}


nclx_profile make_nclx(int matrix_coefficients, bool full_range)
{
  nclx_profile nclx = nclx_profile::defaults();
  nclx.set_matrix_coefficients(static_cast<heif_matrix_coefficients>(matrix_coefficients));
  nclx.set_full_range_flag(full_range);
  return nclx;
}

// The default, GBR, BT.709 with limited range and YCgCo.
std::vector<nclx_profile> nclx_variants()
{
  return {nclx_profile::defaults(), make_nclx(0, true), make_nclx(1, false), make_nclx(8, true)};
}


std::string describe(const ColorState& state)
{
  std::ostringstream ostr;
  ostr << state;
  return ostr.str();
}


// Whether an image can have this state: the planes that the colorspace and the chroma format
// imply, and no others.
bool is_image_state(const ColorState& s)
{
  const bool ycbcr_planes = s.bits_per_pixel_Y || s.bits_per_pixel_Cb || s.bits_per_pixel_Cr;
  const bool rgb_planes = s.bits_per_pixel_R || s.bits_per_pixel_G || s.bits_per_pixel_B;

  if (s.colorspace == heif_colorspace_filter_array) {
    return s.chroma == heif_chroma_planar && s.bits_per_pixel_filter_array && !ycbcr_planes && !rgb_planes &&
           !s.bits_per_pixel_alpha;
  }

  if (s.bits_per_pixel_filter_array) {
    return false;
  }

  switch (s.colorspace) {
    case heif_colorspace_YCbCr:
      return (s.chroma == heif_chroma_420 || s.chroma == heif_chroma_422 || s.chroma == heif_chroma_444) &&
             !rgb_planes && s.bits_per_pixel_Y && s.bits_per_pixel_Cb && s.bits_per_pixel_Cr;

    case heif_colorspace_monochrome:
      return s.chroma == heif_chroma_monochrome && !rgb_planes &&
             s.bits_per_pixel_Y && !s.bits_per_pixel_Cb && !s.bits_per_pixel_Cr;

    case heif_colorspace_RGB: {
      if (ycbcr_planes || !s.bits_per_pixel_R || !s.bits_per_pixel_G || !s.bits_per_pixel_B) {
        return false;
      }
      if (s.chroma == heif_chroma_444) {
        return true;
      }
      if (!is_interleaved(s.chroma)) {
        return false;
      }

      const int bpp = s.bits_per_pixel_R;
      if (s.bits_per_pixel_G != bpp || s.bits_per_pixel_B != bpp) {
        return false;
      }
      if (s.bits_per_pixel_alpha != (is_interleaved_with_alpha(s.chroma) ? bpp : 0)) {
        return false;
      }
      if (s.chroma == heif_chroma_interleaved_RGB || s.chroma == heif_chroma_interleaved_RGBA) {
        return bpp == 8;
      }
      return bpp >= 9 && bpp <= 16;
    }

    default:
      return false;
  }
}


uint32_t g_random_state = 1;

uint32_t next_random()
{
  g_random_state = g_random_state * 1664525u + 1013904223u;
  return g_random_state >> 8;
}

// Fills a plane with samples that are within the range of the bit depth.
void fill_plane(uint8_t* mem, size_t stride, uint32_t samples_per_row, uint32_t rows, int bits, bool big_endian)
{
  const int bytes = bytes_per_sample_for_bit_depth(bits);

  for (uint32_t y = 0; y < rows; y++) {
    for (uint32_t x = 0; x < samples_per_row; x++) {
      uint64_t value = next_random();
      if (bits < 24) {
        value &= (uint64_t{1} << bits) - 1;
      }

      uint8_t* p = mem + y * stride + static_cast<size_t>(x) * bytes;
      if (bytes == 2 && big_endian) {
        p[0] = static_cast<uint8_t>(value >> 8);
        p[1] = static_cast<uint8_t>(value);
      }
      else if (bytes == 2) {
        auto v16 = static_cast<uint16_t>(value);
        memcpy(p, &v16, 2);
      }
      else {
        memcpy(p, &value, bytes < 8 ? bytes : 8);
      }
    }
  }
}


std::shared_ptr<HeifPixelImage> make_bayer_image(const ColorState& state, uint32_t w, uint32_t h)
{
  const int bits = state.bits_per_pixel_filter_array;

  heif_image* image = nullptr;
  REQUIRE(heif_image_create(static_cast<int>(w), static_cast<int>(h),
                            heif_colorspace_filter_array, heif_chroma_planar, &image).code == heif_error_Ok);

  uint32_t filter_array_id = 0;
  REQUIRE(heif_image_add_component(image, w, h, heif_cmpd_component_type_filter_array,
                                   heif_component_datatype_unsigned_integer, bits, &filter_array_id).code == heif_error_Ok);

  uint32_t r_id = 0, g_id = 0, b_id = 0;
  REQUIRE(heif_image_add_bayer_component(image, heif_cmpd_component_type_red, &r_id).code == heif_error_Ok);
  REQUIRE(heif_image_add_bayer_component(image, heif_cmpd_component_type_green, &g_id).code == heif_error_Ok);
  REQUIRE(heif_image_add_bayer_component(image, heif_cmpd_component_type_blue, &b_id).code == heif_error_Ok);

  heif_bayer_pattern_pixel pattern[4] = {{r_id, 1.0f}, {g_id, 1.0f}, {g_id, 1.0f}, {b_id, 1.0f}};
  REQUIRE(heif_image_set_bayer_pattern(image, filter_array_id, 2, 2, pattern).code == heif_error_Ok);

  std::shared_ptr<HeifPixelImage> img = image->image;
  heif_image_release(image);

  size_t stride = 0;
  uint8_t* mem = img->get_channel_memory(heif_channel_filter_array, &stride);
  REQUIRE(mem != nullptr);
  fill_plane(mem, stride, w, h, bits, false);

  if (state.bits_per_pixel_alpha) {
    REQUIRE(!img->add_channel(heif_channel_Alpha, w, h, state.bits_per_pixel_alpha, heif_get_disabled_security_limits()));

    mem = img->get_channel_memory(heif_channel_Alpha, &stride);
    REQUIRE(mem != nullptr);
    fill_plane(mem, stride, w, h, state.bits_per_pixel_alpha, std::endian::native == std::endian::big);
  }

  return img;
}


// An image with the planes of 'state'. Returns null if HeifPixelImage refuses to create it.
std::shared_ptr<HeifPixelImage> make_image(const ColorState& state, uint32_t w, uint32_t h)
{
  if (state.colorspace == heif_colorspace_filter_array) {
    return make_bayer_image(state, w, h);
  }

  const heif_security_limits* limits = heif_get_disabled_security_limits();

  auto img = std::make_shared<HeifPixelImage>();
  img->create(w, h, state.colorspace, state.chroma);

  auto add_plane = [&](heif_channel channel, uint32_t plane_w, uint32_t plane_h, int bits, int components) {
    if (img->add_channel(channel, plane_w, plane_h, bits, limits)) {
      return false;
    }

    const bool big_endian = (state.chroma == heif_chroma_interleaved_RRGGBB_BE ||
                             state.chroma == heif_chroma_interleaved_RRGGBBAA_BE);
    // Planar 16-bit samples are in the byte order of the machine.
    const bool store_big_endian = (channel == heif_channel_interleaved) ? big_endian : (std::endian::native == std::endian::big);

    size_t stride = 0;
    uint8_t* mem = img->get_channel_memory(channel, &stride);
    fill_plane(mem, stride, plane_w * components, plane_h, bits, store_big_endian);
    return true;
  };

  if (is_interleaved(state.chroma)) {
    if (!add_plane(heif_channel_interleaved, w, h, state.bits_per_pixel_R, num_interleaved_components_per_plane(state.chroma))) {
      return nullptr;
    }
  }
  else {
    for (heif_channel channel : {heif_channel_Y, heif_channel_Cb, heif_channel_Cr,
                                 heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha}) {
      const int bits = state.get_bits_per_pixel(channel);
      if (bits == 0) {
        continue;
      }

      if (!add_plane(channel, channel_width(w, state.chroma, channel), channel_height(h, state.chroma, channel), bits, 1)) {
        return nullptr;
      }
    }
  }

  img->set_color_profile_nclx(state.nclx);
  return img;
}


// colorspace x chroma format x bit depth x alpha x nclx, whether an image can have that
// combination or not. A target state can be any of these.
std::vector<ColorState> all_states()
{
  std::vector<ColorState> states;

  for (heif_colorspace colorspace : {heif_colorspace_YCbCr, heif_colorspace_RGB, heif_colorspace_monochrome,
                                     heif_colorspace_filter_array, heif_colorspace_undefined}) {
    for (heif_chroma chroma : kChromas) {
      for (int bpp : kDepths) {
        for (int alpha_bpp : {0, bpp, bpp == 8 ? 10 : 8}) {
          for (const nclx_profile& nclx : nclx_variants()) {
            ColorState state;
            state.colorspace = colorspace;
            state.chroma = chroma;
            state.set_color_bits_per_pixel(bpp);
            state.bits_per_pixel_alpha = alpha_bpp;
            state.nclx = nclx;
            states.push_back(state);
          }
        }
      }
    }
  }

  return states;
}


// The states that an image can have.
std::vector<ColorState> image_states()
{
  const nclx_profile default_nclx = nclx_profile::defaults();

  std::vector<ColorState> states;

  for (const ColorState& state : all_states()) {
    if (!is_image_state(state)) {
      continue;
    }

    // The nclx of a state is only looked at for YCbCr.
    if (state.colorspace != heif_colorspace_YCbCr &&
        (state.nclx.get_matrix_coefficients() != default_nclx.get_matrix_coefficients() ||
         state.nclx.get_full_range_flag() != default_nclx.get_full_range_flag())) {
      continue;
    }

    states.push_back(state);
  }

  // planes of different bit depths, as an 'unci' image can have them
  for (heif_chroma chroma : {heif_chroma_420, heif_chroma_444}) {
    ColorState state;
    state.colorspace = heif_colorspace_YCbCr;
    state.chroma = chroma;
    state.nclx = default_nclx;

    state.bits_per_pixel_Y = 10;
    state.bits_per_pixel_Cb = 8;
    state.bits_per_pixel_Cr = 8;
    states.push_back(state);

    state.bits_per_pixel_Y = 8;
    state.bits_per_pixel_Cb = 12;
    state.bits_per_pixel_Cr = 10;
    states.push_back(state);
  }

  {
    ColorState state;
    state.colorspace = heif_colorspace_RGB;
    state.chroma = heif_chroma_444;
    state.nclx = default_nclx;

    state.bits_per_pixel_R = 5;
    state.bits_per_pixel_G = 6;
    state.bits_per_pixel_B = 5;
    states.push_back(state);

    state.bits_per_pixel_R = 8;
    state.bits_per_pixel_G = 10;
    state.bits_per_pixel_B = 8;
    state.bits_per_pixel_alpha = 8;
    states.push_back(state);
  }

  return states;
}


// States that convert_colorspace() refuses at its entry (HeifPixelImage::check_plane_layout()),
// but that an image can be built with, because HeifPixelImage accepts any set of planes: a
// filter array with an alpha plane. What an operator declares for such an input has to hold as
// well. Op_drop_alpha_plane and Op_adjust_alpha_bit_depth took the chroma format of a filter
// array (planar, the same value as monochrome) for a monochrome image, declared the input
// state with another alpha plane and returned an image without the filter-array plane.
std::vector<ColorState> refused_input_states()
{
  std::vector<ColorState> states;

  for (int bpp : {8, 10, 16}) {
    for (int alpha_bpp : {8, 10, 16}) {
      ColorState state;
      state.colorspace = heif_colorspace_filter_array;
      state.chroma = heif_chroma_planar;
      state.bits_per_pixel_filter_array = bpp;
      state.bits_per_pixel_alpha = alpha_bpp;
      state.nclx = nclx_profile::defaults();
      states.push_back(state);
    }
  }

  return states;
}


struct Options
{
  heif_color_conversion_options options;
  heif_alpha_composition_mode alpha_mode;
};

std::vector<Options> option_sets()
{
  std::vector<Options> sets;

  heif_color_conversion_options options;
  heif_color_conversion_options_set_defaults(&options);

  // only the preferred chroma algorithms, alpha is kept
  options.only_use_preferred_chroma_algorithm = true;
  sets.push_back({options, heif_alpha_composition_mode_none});

  // any chroma algorithm, alpha is composited onto a background
  options.only_use_preferred_chroma_algorithm = false;
  sets.push_back({options, heif_alpha_composition_mode_solid_color});

  return sets;
}

} // namespace


TEST_CASE("conversion operators return the image that they declared")
{
  const std::vector<ColorState> targets = all_states();
  const heif_security_limits* limits = heif_get_disabled_security_limits();

  std::vector<ColorState> inputs = image_states();
  for (const ColorState& state : refused_input_states()) {
    inputs.push_back(state);
  }

  size_t num_operators_used = 0;
  size_t num_conversions = 0;
  size_t num_contradictions = 0;
  std::string examples;

  for (const auto& op : ColorConversionPipeline::get_operations()) {
    const auto& op_ref = *op;
    const std::string op_name = typeid(op_ref).name();
    size_t num_conversions_of_operator = 0;

    for (const Options& opt : option_sets()) {
      OptionsExt ext = make_options_ext(opt.alpha_mode);

      for (const ColorState& input_state : inputs) {
        // The same output state is declared for many target states. Run each conversion once.
        std::set<std::string> converted;

        for (const ColorState& target_state : targets) {
          for (const ColorStateWithCost& declared : op->state_after_conversion(input_state, target_state, opt.options, *ext)) {
            if (!converted.insert(describe(declared.color_state)).second) {
              continue;
            }

            // an even and an odd image size
            for (auto size : {std::pair<uint32_t, uint32_t>{18, 10}, std::pair<uint32_t, uint32_t>{7, 5}}) {
              auto image = make_image(input_state, size.first, size.second);
              REQUIRE(image != nullptr);

              auto result = op->convert_colorspace(image, input_state, declared.color_state, opt.options, *ext, limits);
              if (!result) {
                // Refusing the conversion does not contradict the declaration.
                continue;
              }

              num_conversions++;
              num_conversions_of_operator++;

              if (ColorConversionPipeline::check_operation_output(*result, declared.color_state)) {
                num_contradictions++;
                if (num_contradictions <= 5) {
                  examples += op_name + "\n  input:    " + describe(input_state) +
                              "\n  declared: " + describe(declared.color_state) +
                              "\n  returned: " + describe(ColorState::from_image_planes(**result)) + "\n";
                }
              }
              else {
                CHECK((*result)->get_width() == size.first);
                CHECK((*result)->get_height() == size.second);
              }
            }
          }
        }
      }
    }

    if (num_conversions_of_operator > 0) {
      num_operators_used++;
    }
  }

  INFO("first contradictions:\n" << examples);
  CHECK(num_contradictions == 0);

  // The test is of no use if the operators decline everything they are offered.
  CHECK(num_operators_used >= 25);
  CHECK(num_conversions >= 2000);
}


TEST_CASE("Op_RGB_to_YCbCr only declares planar YCbCr chroma formats")
{
  heif_color_conversion_options options;
  heif_color_conversion_options_set_defaults(&options);
  options.only_use_preferred_chroma_algorithm = false;
  OptionsExt ext = make_options_ext(heif_alpha_composition_mode_none);

  for (int bpp : {8, 10}) {
    ColorState input(heif_colorspace_RGB, heif_chroma_444, false, bpp);
    input.nclx = nclx_profile::defaults();

    auto declared_states = [&](heif_colorspace colorspace, heif_chroma chroma) {
      ColorState target(colorspace, chroma, false, bpp);
      target.nclx = nclx_profile::defaults();

      if (bpp == 8) {
        return Op_RGB_to_YCbCr<uint8_t>().state_after_conversion(input, target, options, *ext);
      }
      else {
        return Op_RGB_to_YCbCr<uint16_t>().state_after_conversion(input, target, options, *ext);
      }
    };

    for (heif_chroma chroma : {heif_chroma_420, heif_chroma_422, heif_chroma_444}) {
      auto states = declared_states(heif_colorspace_YCbCr, chroma);
      REQUIRE(states.size() == 1);
      CHECK(states[0].color_state.colorspace == heif_colorspace_YCbCr);
      CHECK(states[0].color_state.chroma == chroma);
      CHECK(is_image_state(states[0].color_state));
    }

    // It used to declare a YCbCr state with the chroma format of these targets.
    CHECK(declared_states(heif_colorspace_monochrome, heif_chroma_monochrome).empty());
    CHECK(declared_states(heif_colorspace_RGB, heif_chroma_interleaved_RGB).empty());
    CHECK(declared_states(heif_colorspace_RGB, heif_chroma_interleaved_RGBA).empty());
    CHECK(declared_states(heif_colorspace_RGB, heif_chroma_interleaved_RRGGBB_LE).empty());
    CHECK(declared_states(heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_BE).empty());
  }
}


TEST_CASE("the pipeline refuses an operator result that contradicts the declared state")
{
  const heif_security_limits* limits = heif_get_disabled_security_limits();
  const uint32_t w = 18, h = 10;

  // An image with exactly these planes.
  auto image_with_planes = [&](heif_colorspace colorspace, heif_chroma chroma,
                               std::initializer_list<std::pair<heif_channel, int>> planes,
                               uint32_t chroma_w, uint32_t chroma_h) {
    auto img = std::make_shared<HeifPixelImage>();
    img->create(w, h, colorspace, chroma);
    for (const auto& plane : planes) {
      const bool is_chroma = (plane.first == heif_channel_Cb || plane.first == heif_channel_Cr);
      REQUIRE(!img->add_channel(plane.first, is_chroma ? chroma_w : w, is_chroma ? chroma_h : h, plane.second, limits));
    }
    return img;
  };

  const ColorState ycbcr420(heif_colorspace_YCbCr, heif_chroma_420, false, 8);
  const ColorState ycbcr420_alpha(heif_colorspace_YCbCr, heif_chroma_420, true, 8);

  auto good = image_with_planes(heif_colorspace_YCbCr, heif_chroma_420,
                                {{heif_channel_Y, 8}, {heif_channel_Cb, 8}, {heif_channel_Cr, 8}}, 9, 5);

  SECTION("an image with the declared planes is accepted") {
    CHECK(!ColorConversionPipeline::check_operation_output(good, ycbcr420));
  }

  SECTION("no image") {
    CHECK(ColorConversionPipeline::check_operation_output(nullptr, ycbcr420));
  }

  SECTION("what Op_RGB_to_YCbCr returned for a monochrome target") {
    // declared: YCbCr with the monochrome chroma format and a Y plane only
    ColorState declared;
    declared.colorspace = heif_colorspace_YCbCr;
    declared.chroma = heif_chroma_monochrome;
    declared.set_color_bits_per_pixel(8);
    REQUIRE(declared.bits_per_pixel_Cb == 0);

    auto returned = image_with_planes(heif_colorspace_YCbCr, heif_chroma_monochrome,
                                      {{heif_channel_Y, 8}, {heif_channel_Cb, 8}, {heif_channel_Cr, 8}}, w, h);
    CHECK(ColorConversionPipeline::check_operation_output(returned, declared));
  }

  SECTION("a plane that was not declared") {
    auto with_alpha = image_with_planes(heif_colorspace_YCbCr, heif_chroma_420,
                                        {{heif_channel_Y, 8}, {heif_channel_Cb, 8}, {heif_channel_Cr, 8}, {heif_channel_Alpha, 8}}, 9, 5);
    CHECK(ColorConversionPipeline::check_operation_output(with_alpha, ycbcr420));
    CHECK(!ColorConversionPipeline::check_operation_output(with_alpha, ycbcr420_alpha));
  }

  SECTION("a declared plane is missing") {
    CHECK(ColorConversionPipeline::check_operation_output(good, ycbcr420_alpha));
  }

  SECTION("another bit depth") {
    // A plane with 8-bit samples that the next operator would read with 16-bit samples.
    CHECK(ColorConversionPipeline::check_operation_output(good, ColorState(heif_colorspace_YCbCr, heif_chroma_420, false, 10)));

    auto mixed = image_with_planes(heif_colorspace_YCbCr, heif_chroma_420,
                                   {{heif_channel_Y, 10}, {heif_channel_Cb, 8}, {heif_channel_Cr, 8}}, 9, 5);
    CHECK(ColorConversionPipeline::check_operation_output(mixed, ColorState(heif_colorspace_YCbCr, heif_chroma_420, false, 10)));
  }

  SECTION("another chroma format or colorspace") {
    CHECK(ColorConversionPipeline::check_operation_output(good, ColorState(heif_colorspace_YCbCr, heif_chroma_422, false, 8)));
    CHECK(ColorConversionPipeline::check_operation_output(good, ColorState(heif_colorspace_RGB, heif_chroma_444, false, 8)));
  }

  SECTION("planes that do not have the size of the image") {
    // The planes and their bit depths are the declared ones, but one plane is larger.
    auto img = std::make_shared<HeifPixelImage>();
    img->create(w, h, heif_colorspace_RGB, heif_chroma_444);
    REQUIRE(!img->add_channel(heif_channel_R, w, h, 8, limits));
    REQUIRE(!img->add_channel(heif_channel_G, w, h, 8, limits));
    REQUIRE(!img->add_channel(heif_channel_B, 2 * w, 2 * h, 8, limits));

    CHECK(ColorConversionPipeline::check_operation_output(img, ColorState(heif_colorspace_RGB, heif_chroma_444, false, 8)));
  }
}


TEST_CASE("a successful color conversion returns the requested format")
{
  const heif_security_limits* limits = heif_get_disabled_security_limits();

  size_t num_converted = 0;

  for (const Options& opt : option_sets()) {
    OptionsExt ext = make_options_ext(opt.alpha_mode);

    for (const ColorState& input_state : image_states()) {
      auto image = make_image(input_state, 18, 10);
      REQUIRE(image != nullptr);

      // Any combination of colorspace and chroma format can be requested, also those that
      // no image can have.
      for (heif_colorspace colorspace : {heif_colorspace_YCbCr, heif_colorspace_RGB, heif_colorspace_monochrome}) {
        for (heif_chroma chroma : kChromas) {
          for (int output_bpp : {0, 8, 10}) {
            auto result = convert_colorspace(image, colorspace, chroma, nclx_profile::undefined(), output_bpp,
                                             opt.options, ext.get(), limits);
            if (!result) {
              // An unsupported conversion is refused, but no conversion step may have
              // returned something else than it declared.
              if (result.error().message.find("did not return the image format it declared") != std::string::npos) {
                INFO("input: " << describe(input_state) << " requested colorspace " << colorspace << " chroma " << chroma);
                FAIL_CHECK(result.error().message);
              }
              continue;
            }

            num_converted++;

            const HeifPixelImage& out = **result;
            if (out.get_colorspace() != colorspace || out.get_chroma_format() != chroma || out.check_plane_layout()) {
              INFO("input: " << describe(input_state) << " requested colorspace " << colorspace << " chroma " << chroma);
              FAIL_CHECK("the converted image does not have the requested format");
            }
          }
        }
      }
    }
  }

  CHECK(num_converted >= 2000);
}
