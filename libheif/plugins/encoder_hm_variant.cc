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

// One variant of the HEVC reference software HM behind the interface of encoder_hm_variant.h.
//
// This file is compiled once for each HM variant, with
//
//   HEIF_HM_NAMESPACE         the namespace of the variant
//   HEIF_HM_VARIANT_FUNCTION  the name of the function that returns the variant
//   HEIF_HM_VARIANT_HAS_SCC   whether it has the screen content coding tools
//   HEIF_HM_SOURCE_WIDTH, HEIF_HM_SOURCE_HEIGHT  names of two members of TAppEncCfg
//
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

#include "encoder_hm_variant.h"

#include <new>

// HM is compiled for high bit depths, which changes the data types in its headers. All
// translation units of a variant have to agree on this, CMake defines it for all of them.
#if !defined(RExt__HIGH_BIT_DEPTH_SUPPORT) || !RExt__HIGH_BIT_DEPTH_SUPPORT
#error "HM has to be compiled with RExt__HIGH_BIT_DEPTH_SUPPORT=1"
#endif

// From here on, printf() and exit() are the replacements of hm_as_library.h. They are part
// of the inline functions in the HM headers, which have to be the same in all translation
// units.
#include "hm_as_library.h"

namespace HEIF_HM_NAMESPACE {

#include "TAppEncTop.h"

// The members of TAppEncCfg that hold the size of the padded picture have different names
// in the versions of HM. CMake looks them up.
#if !defined(HEIF_HM_SOURCE_WIDTH) || !defined(HEIF_HM_SOURCE_HEIGHT)
#error "HEIF_HM_SOURCE_WIDTH and HEIF_HM_SOURCE_HEIGHT have to be defined"
#endif


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

    original.create(HEIF_HM_SOURCE_WIDTH, HEIF_HM_SOURCE_HEIGHT, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);
    true_original.create(HEIF_HM_SOURCE_WIDTH, HEIF_HM_SOURCE_HEIGHT, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);

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
    reconstruction->create(HEIF_HM_SOURCE_WIDTH, HEIF_HM_SOURCE_HEIGHT, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);

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


static hm_variant_result encode(const std::vector<std::string>& options,
                                const heif_image* image,
                                std::deque<std::vector<uint8_t>>& out_nal_units)
{
  hm_variant_result result;

  heif_hm_messages().clear();

  try {
    HmEncoder hm;

    if (!hm.configure(options)) {
      result.status = hm_variant_result::Status::configuration_refused;
    }
    else {
      hm.encode(image, out_nal_units);
    }
  }
  catch (const heif_hm_exit&) {
    result.status = hm_variant_result::Status::encoding_failed;
  }
  catch (const std::bad_alloc&) {
    result.status = hm_variant_result::Status::out_of_memory;
  }
  catch (const std::exception& e) {
    result.status = hm_variant_result::Status::encoding_failed;
    heif_hm_messages() = e.what();
  }

  if (result.status != hm_variant_result::Status::ok) {
    out_nal_units.clear();
    result.messages = heif_hm_messages();
  }

  heif_hm_messages().clear();

  return result;
}


static const hm_variant variant{
    NV_VERSION,
    HEIF_HM_VARIANT_HAS_SCC != 0,
    encode
};

} // namespace HEIF_HM_NAMESPACE


const hm_variant* HEIF_HM_VARIANT_FUNCTION()
{
  return &HEIF_HM_NAMESPACE::variant;
}
