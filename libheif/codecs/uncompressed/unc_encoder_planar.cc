/*
 * ImageMeter confidential
 *
 * Copyright (C) 2026 by Dirk Farin, Kronenstr. 49b, 70174 Stuttgart, Germany
 * All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains the property
 * of Dirk Farin.  The intellectual and technical concepts contained
 * herein are proprietary to Dirk Farin and are protected by trade secret
 * and copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Dirk Farin.
 */

#include "unc_encoder_planar.h"

#include <cstring>

#include "pixelimage.h"
#include "unc_boxes.h"


bool unc_encoder_planar::can_encode(const std::shared_ptr<const HeifPixelImage>& image,
                                    const heif_encoding_options& options) const
{
  if (image->has_channel(heif_channel_interleaved)) {
    return false;
  }

  return true;
}


heif_uncompressed_component_type heif_channel_to_component_type(heif_channel channel)
{
  switch (channel) {
    case heif_channel_Y: return heif_uncompressed_component_type::component_type_Y;
    case heif_channel_Cb: return heif_uncompressed_component_type::component_type_Cb;
    case heif_channel_Cr: return heif_uncompressed_component_type::component_type_Cr;
    case heif_channel_R: return heif_uncompressed_component_type::component_type_red;
    case heif_channel_G: return heif_uncompressed_component_type::component_type_green;
    case heif_channel_B: return heif_uncompressed_component_type::component_type_blue;
    case heif_channel_Alpha: return heif_uncompressed_component_type::component_type_alpha;
    case heif_channel_interleaved: assert(false); break;
    case heif_channel_filter_array:  return heif_uncompressed_component_type::component_type_filter_array;
    case heif_channel_depth: return heif_uncompressed_component_type::component_type_depth;
    case heif_channel_disparity: return heif_uncompressed_component_type::component_type_disparity;
  }

  return heif_uncompressed_component_type::component_type_padded;
}


struct channel_component
{
  heif_channel channel;
  heif_uncompressed_component_type component_type;
};

void add_channel_if_exists(const std::shared_ptr<const HeifPixelImage>& image, std::vector<channel_component>& list, heif_channel channel)
{
  if (image->has_channel(channel)) {
    list.push_back({channel, heif_channel_to_component_type(channel)});
  }
}

std::vector<channel_component> get_channels(const std::shared_ptr<const HeifPixelImage>& image)
{
  std::vector<channel_component> channels;

  // Special case for heif_channel_Y:
  // - if this an YCbCr image, use component_type_Y,
  // - otherwise, use component_type_monochrome

  if (image->has_channel(heif_channel_Y)) {
    if (image->has_channel(heif_channel_Cb) && image->has_channel(heif_channel_Cr)) {
      channels.push_back({heif_channel_Y, heif_uncompressed_component_type::component_type_Y});
    }
    else {
      channels.push_back({heif_channel_Y, heif_uncompressed_component_type::component_type_monochrome});
    }
  }

  add_channel_if_exists(image, channels, heif_channel_Cb);
  add_channel_if_exists(image, channels, heif_channel_Cr);
  add_channel_if_exists(image, channels, heif_channel_R);
  add_channel_if_exists(image, channels, heif_channel_G);
  add_channel_if_exists(image, channels, heif_channel_B);
  add_channel_if_exists(image, channels, heif_channel_Alpha);
  add_channel_if_exists(image, channels, heif_channel_filter_array);
  add_channel_if_exists(image, channels, heif_channel_depth);
  add_channel_if_exists(image, channels, heif_channel_disparity);

  return channels;
}

void unc_encoder_planar::fill_cmpd_and_uncC(std::shared_ptr<Box_cmpd>& cmpd,
                                            std::shared_ptr<Box_uncC>& uncC,
                                            const std::shared_ptr<const HeifPixelImage>& image,
                                            const heif_encoding_options& options) const
{
  auto channels = get_channels(image);

  // if we have any component > 8 bits, we enable this
  bool little_endian = false;

  uint16_t index=0;
  for (channel_component channelcomponent : channels) {
    cmpd->add_component({channelcomponent.component_type});

    uint8_t bpp = image->get_bits_per_pixel(channelcomponent.channel);
    uint8_t component_align_size = static_cast<uint8_t>((bpp + 7) / 8);

    if (bpp % 8 == 0) {
      component_align_size = 0;
    }

    if (bpp > 8) {
      little_endian = true; // TODO: depending on the host endianness
    }

    uncC->add_component({index, bpp, component_format_unsigned, component_align_size});
    index++;
  }

  uncC->set_interleave_type(interleave_mode_component);
  uncC->set_components_little_endian(little_endian);

  if (image->get_chroma_format() == heif_chroma_420) {
    uncC->set_sampling_type(2);
  }
  else if (image->get_chroma_format() == heif_chroma_422) {
    uncC->set_sampling_type(1);
  }
  else {
    uncC->set_sampling_type(0);
  }
}


std::vector<uint8_t> unc_encoder_planar::encode_tile(const std::shared_ptr<const HeifPixelImage>& src_image,
                                                     const heif_encoding_options& options) const
{
  std::vector<uint8_t> data;

  auto channels = get_channels(src_image);

  // compute total size of all components

  uint64_t total_size = 0;

  for (channel_component channelcomponent : channels) {
    int bpp = src_image->get_bits_per_pixel(channelcomponent.channel);
    int bytes_per_pixel = (bpp + 7) / 8;

    total_size += static_cast<uint64_t>(src_image->get_height(channelcomponent.channel)) * src_image->get_width(channelcomponent.channel) * bytes_per_pixel;
  }

  data.resize(total_size);

  // output all component planes

  uint64_t out_data_start_pos=0;

  for (channel_component channelcomponent : channels) {
    int bpp = src_image->get_bits_per_pixel(channelcomponent.channel);
    int bytes_per_pixel = (bpp + 7) / 8;

    size_t src_stride;
    const uint8_t* src_data = src_image->get_plane(channelcomponent.channel, &src_stride);

    for (uint32_t y = 0; y < src_image->get_height(channelcomponent.channel); y++) {
      uint32_t width = src_image->get_width(channelcomponent.channel);
      memcpy(data.data() + out_data_start_pos, src_data + src_stride * y, width * bytes_per_pixel);
      out_data_start_pos += width * bytes_per_pixel;
    }
  }

  return data;
}
