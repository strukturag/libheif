/*
 * OpenJPH High Throughput JPEG 2000 decoder.
 *
 * Copyright (c) 2024 Brad Hards
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
 */

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "decoder_openjph.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

#include <openjph/ojph_arch.h>
#include <openjph/ojph_codestream.h>
#include <openjph/ojph_defs.h>
#include <openjph/ojph_file.h>
#include <openjph/ojph_mem.h>
#include <openjph/ojph_params.h>
#include <openjph/ojph_version.h>


struct openjph_dec_context
{
    std::vector<uint8_t> data;
    bool strict_decoding = false;
};


static const int OPENJPH_DEC_PLUGIN_PRIORITY = 100;

static void openjph_dec_init_plugin()
{
}

static void openjph_dec_deinit_plugin()
{
}

static int openjph_dec_does_support_format(enum heif_compression_format format)
{
    if (format == heif_compression_HTJ2K) {
        return OPENJPH_DEC_PLUGIN_PRIORITY;
    } else {
        return 0;
    }
}

struct heif_error openjph_dec_new_decoder(void **dec)
{
    struct openjph_dec_context *decoder_context = new openjph_dec_context();
    *dec = decoder_context;
    return heif_error_ok;
}

void openjph_dec_free_decoder(void *decoder_raw)
{
    openjph_dec_context *decoder_context = (openjph_dec_context *)decoder_raw;
    delete decoder_context;
}

void openjph_dec_set_strict_decoding(void *decoder_raw, int flag)
{
    openjph_dec_context *decoder_context = (openjph_dec_context *)decoder_raw;
    decoder_context->strict_decoding = flag;
}

struct heif_error openjph_dec_push_data(void *decoder_raw, const void *frame_data, size_t frame_size)
{
    openjph_dec_context *decoder_context = (openjph_dec_context *)decoder_raw;
    const uint8_t* data = (const uint8_t*)frame_data;
    decoder_context->data.insert(decoder_context->data.end(), data, data + frame_size);
    return heif_error_ok;
}

struct heif_error openjph_dec_decode_image(void *decoder_raw, struct heif_image **out_img)
{
    openjph_dec_context *decoder_context = (openjph_dec_context *)decoder_raw;
    ojph::codestream codestream;
    ojph::mem_infile input;
    input.open(decoder_context->data.data(), decoder_context->data.size());
    if (!(decoder_context->strict_decoding)) {
        codestream.enable_resilience();
    }
    codestream.read_headers(&input);
    codestream.create();

    struct heif_image* heif_img = nullptr;

    ojph::param_siz siz = codestream.access_siz();
    ojph::point imageExtent = siz.get_image_extent();
    ojph::point imageOffset = siz.get_image_offset();
    uint32_t width = imageExtent.x - imageOffset.x;
    uint32_t height = imageExtent.y - imageOffset.y;

    // TODO: work out colorspace and chroma correctly
    heif_colorspace colourspace = heif_colorspace_undefined;
    heif_chroma chroma = heif_chroma_undefined;
    if (siz.get_num_components() == 3) {
        colourspace = heif_colorspace_YCbCr;
        chroma = heif_chroma_444;
        // check components after the first one
        for (unsigned int i = 1; i < siz.get_num_components(); ++i) {
            ojph::point downsampling = siz.get_downsampling(i);
            if ((downsampling.x == 1) && (downsampling.y == 1)) {
                // 4:4:4
                if (chroma != heif_chroma_444) {
                    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "mismatched chroma format"};
                    return err;
                }
            } else if ((downsampling.x == 2) && (downsampling.y == 1)) {
                // 4:2:2
                if ((chroma == heif_chroma_444) && (i == 1)) {
                    chroma = heif_chroma_422;
                } else if (chroma != heif_chroma_422) {
                    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "mismatched chroma format"};
                    return err;
                }
            } else if ((downsampling.x == 2) && (downsampling.y == 2)) {
                // 4:2:0
                if ((chroma == heif_chroma_444) && (i == 1)) {
                    chroma = heif_chroma_420;
                } else if (chroma != heif_chroma_420) {
                    struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "mismatched chroma format"};
                    return err;
                }

            }
        }
    } else if (siz.get_num_components() == 1) {
        colourspace = heif_colorspace_monochrome;
        chroma = heif_chroma_monochrome;
    } else {
        struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported number of components"};
        return err;

    }

    struct heif_error err = heif_image_create(width, height, colourspace, chroma, &heif_img);
    if (err.code != heif_error_Ok) {
      assert(heif_img == nullptr);
      return err;
    }

    // TODO: map component to channel
    if (colourspace == heif_colorspace_monochrome) {
        heif_image_add_plane(heif_img, heif_channel_Y, width, height, siz.get_bit_depth(0));
        // TODO: make image
        struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported monochrome image"};
        return err;
    } else {
        if (codestream.is_planar()) {
            heif_channel channels[] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
            for (uint32_t componentIndex = 0; componentIndex < siz.get_num_components(); ++componentIndex) {
                uint32_t component_width = siz.get_recon_width(componentIndex);
                uint32_t component_height = siz.get_recon_height(componentIndex);
                uint32_t bit_depth = siz.get_bit_depth(componentIndex);
                heif_channel channel = channels[componentIndex];
                heif_image_add_plane(heif_img, channel, component_width, component_height, bit_depth);
                int planeStride;
                uint8_t* plane = heif_image_get_plane(heif_img, channel, &planeStride);
                for (uint32_t rowIndex = 0; rowIndex < component_height; rowIndex++) {
                    uint32_t comp_num;
                    ojph::line_buf *line = codestream.pull(comp_num);
                    const int32_t *cursor = line->i32;
                    for (uint32_t colIndex = 0; colIndex < component_width; ++colIndex) {
                        int v = *cursor;
                        // TODO: this only works for the 8 bit case
                        plane[rowIndex * planeStride + colIndex] = (uint8_t) v;
                        cursor++;
                    }
                }
            }
        } else {
            struct heif_error err = {heif_error_Decoder_plugin_error, heif_suberror_Unspecified, "unsupported interleaved image"};
            return err;
        }
    }
    *out_img = heif_img;
    return heif_error_ok;
}

static const int MAX_PLUGIN_NAME_LENGTH = 80;
static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

const char *openjph_dec_plugin_name()
{
    snprintf(plugin_name, MAX_PLUGIN_NAME_LENGTH,
             "OpenJPH %s.%s.%s",
             OJPH_INT_TO_STRING(OPENJPH_VERSION_MAJOR),
             OJPH_INT_TO_STRING(OPENJPH_VERSION_MINOR),
             OJPH_INT_TO_STRING(OPENJPH_VERSION_PATCH));
    plugin_name[MAX_PLUGIN_NAME_LENGTH - 1] = 0;

    return plugin_name;
}

static const struct heif_decoder_plugin decoder_openjph
{
    3,
        openjph_dec_plugin_name,
        openjph_dec_init_plugin,
        openjph_dec_deinit_plugin,
        openjph_dec_does_support_format,
        openjph_dec_new_decoder,
        openjph_dec_free_decoder,
        openjph_dec_push_data,
        openjph_dec_decode_image,
        openjph_dec_set_strict_decoding,
        "openjph"
};

const struct heif_decoder_plugin *get_decoder_plugin_openjph()
{
    return &decoder_openjph;
}

#if PLUGIN_OPENJPH_DECODER
heif_plugin_info plugin_info{
    1,
    heif_plugin_type_decoder,
    &decoder_openjph};
#endif
