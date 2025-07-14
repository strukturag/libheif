/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "heif_decoding.h"
#include "api_structs.h"
#include "plugin_registry.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>


void heif_context_set_max_decoding_threads(struct heif_context* ctx, int max_threads)
{
  ctx->context->set_max_decoding_threads(max_threads);
}


int heif_have_decoder_for_format(enum heif_compression_format format)
{
  auto plugin = get_decoder(format, nullptr);
  return plugin != nullptr;
}


static void fill_default_decoding_options(heif_decoding_options& options)
{
  options.version = 7;

  options.ignore_transformations = false;

  options.start_progress = nullptr;
  options.on_progress = nullptr;
  options.end_progress = nullptr;
  options.progress_user_data = nullptr;

  // version 2

  options.convert_hdr_to_8bit = false;

  // version 3

  options.strict_decoding = false;

  // version 4

  options.decoder_id = nullptr;

  // version 5

  options.color_conversion_options.version = 1;
  options.color_conversion_options.preferred_chroma_downsampling_algorithm = heif_chroma_downsampling_average;
  options.color_conversion_options.preferred_chroma_upsampling_algorithm = heif_chroma_upsampling_bilinear;
  options.color_conversion_options.only_use_preferred_chroma_algorithm = false;

  // version 6

  options.cancel_decoding = nullptr;

  // version 7

  options.color_conversion_options_ext = nullptr;
}


heif_decoding_options* heif_decoding_options_alloc()
{
  auto options = new heif_decoding_options;

  fill_default_decoding_options(*options);

  return options;
}


// overwrite the (possibly lower version) input options over the default options
void heif_decoding_options_copy(struct heif_decoding_options* dst,
                                const struct heif_decoding_options* src)
{
  if (src == nullptr) {
    return;
  }

  int min_version = std::min(dst->version, src->version);

  switch (min_version) {
    case 7:
      dst->color_conversion_options_ext = src->color_conversion_options_ext;
      [[fallthrough]];
    case 6:
      dst->cancel_decoding = src->cancel_decoding;
      [[fallthrough]];
    case 5:
      dst->color_conversion_options = src->color_conversion_options;
      [[fallthrough]];
    case 4:
      dst->decoder_id = src->decoder_id;
      [[fallthrough]];
    case 3:
      dst->strict_decoding = src->strict_decoding;
      [[fallthrough]];
    case 2:
      dst->convert_hdr_to_8bit = src->convert_hdr_to_8bit;
      [[fallthrough]];
    case 1:
      dst->ignore_transformations = src->ignore_transformations;
      dst->start_progress = src->start_progress;
      dst->on_progress = src->on_progress;
      dst->end_progress = src->end_progress;
      dst->progress_user_data = src->progress_user_data;
  }
}


void heif_decoding_options_free(heif_decoding_options* options)
{
  delete options;
}


int heif_get_decoder_descriptors(enum heif_compression_format format_filter,
                                 const struct heif_decoder_descriptor** out_decoders,
                                 int count)
{
  struct decoder_with_priority
  {
    const heif_decoder_plugin* plugin;
    int priority;
  };

  std::vector<decoder_with_priority> plugins;
  std::vector<heif_compression_format> formats;
  if (format_filter == heif_compression_undefined) {
    formats = {heif_compression_HEVC, heif_compression_AV1, heif_compression_JPEG, heif_compression_JPEG2000, heif_compression_HTJ2K, heif_compression_VVC};
  }
  else {
    formats.emplace_back(format_filter);
  }

  for (const auto* plugin : get_decoder_plugins()) {
    for (auto& format : formats) {
      int priority = plugin->does_support_format(format);
      if (priority) {
        plugins.push_back({plugin, priority});
        break;
      }
    }
  }

  if (out_decoders == nullptr) {
    return (int) plugins.size();
  }

  std::sort(plugins.begin(), plugins.end(), [](const decoder_with_priority& a, const decoder_with_priority& b) {
    return a.priority > b.priority;
  });

  int nDecodersReturned = std::min(count, (int) plugins.size());

  for (int i = 0; i < nDecodersReturned; i++) {
    out_decoders[i] = (heif_decoder_descriptor*) (plugins[i].plugin);
  }

  return nDecodersReturned;
}


const char* heif_decoder_descriptor_get_name(const struct heif_decoder_descriptor* descriptor)
{
  auto decoder = (heif_decoder_plugin*) descriptor;
  return decoder->get_plugin_name();
}


const char* heif_decoder_descriptor_get_id_name(const struct heif_decoder_descriptor* descriptor)
{
  auto decoder = (heif_decoder_plugin*) descriptor;
  if (decoder->plugin_api_version < 3) {
    return nullptr;
  }
  else {
    return decoder->id_name;
  }
}


struct heif_error heif_decode_image(const struct heif_image_handle* in_handle,
                                    struct heif_image** out_img,
                                    heif_colorspace colorspace,
                                    heif_chroma chroma,
                                    const struct heif_decoding_options* input_options)
{
  if (out_img == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "NULL out_img passed to heif_decode_image()"};
  }

  if (in_handle == nullptr) {
    return {heif_error_Usage_error,
            heif_suberror_Null_pointer_argument,
            "NULL heif_image_handle passed to heif_decode_image()"};
  }

  *out_img = nullptr;
  heif_item_id id = in_handle->image->get_id();

  heif_decoding_options dec_options;
  fill_default_decoding_options(dec_options);
  heif_decoding_options_copy(&dec_options, input_options);

  Result<std::shared_ptr<HeifPixelImage>> decodingResult = in_handle->context->decode_image(id,
                                                                                            colorspace,
                                                                                            chroma,
                                                                                            dec_options,
                                                                                            false, 0, 0);
  if (decodingResult.error.error_code != heif_error_Ok) {
    return decodingResult.error.error_struct(in_handle->image.get());
  }

  std::shared_ptr<HeifPixelImage> img = decodingResult.value;

  *out_img = new heif_image();
  (*out_img)->image = std::move(img);

  return Error::Ok.error_struct(in_handle->image.get());
}



