/*
 * JPEG codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "decoder_jpeg.h"
#include <memory>
#include <cstring>
#include <cassert>
#include <vector>
#include <cstdio>

extern "C" {
#include <jpeglib.h>
}


struct jpeg_decoder
{
  std::vector<uint8_t> data;
};

static const char kSuccess[] = "Success";

static const int JPEG_PLUGIN_PRIORITY = 100;

#define MAX_PLUGIN_NAME_LENGTH 80

static char plugin_name[MAX_PLUGIN_NAME_LENGTH];

#define xstr(s) str(s)
#define str(s) #s

static const char* jpeg_plugin_name()
{
#ifdef LIBJPEG_TURBO_VERSION
  snprintf(plugin_name, MAX_PLUGIN_NAME_LENGTH-1, "libjpeg-turbo " xstr(LIBJPEG_TURBO_VERSION) " (libjpeg %d.%d)", JPEG_LIB_VERSION/10, JPEG_LIB_VERSION%10);
  plugin_name[MAX_PLUGIN_NAME_LENGTH-1] = 0;
#else
  sprintf(plugin_name, "libjpeg %d.%d", JPEG_LIB_VERSION/10, JPEG_LIB_VERSION%10);
#endif

  return plugin_name;
}


static void jpeg_init_plugin()
{
}


static void jpeg_deinit_plugin()
{
}


static int jpeg_does_support_format(enum heif_compression_format format)
{
  if (format == heif_compression_JPEG) {
    return JPEG_PLUGIN_PRIORITY;
  }
  else {
    return 0;
  }
}


struct heif_error jpeg_new_decoder(void** dec)
{
  struct jpeg_decoder* decoder = new jpeg_decoder();
  *dec = decoder;

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


void jpeg_free_decoder(void* decoder_raw)
{
  struct jpeg_decoder* decoder = (jpeg_decoder*) decoder_raw;

  if (!decoder) {
    return;
  }

  delete decoder;
}


void jpeg_set_strict_decoding(void* decoder_raw, int flag)
{
//  struct jpeg_decoder* decoder = (jpeg_decoder*) decoder_raw;
}


struct heif_error jpeg_push_data(void* decoder_raw, const void* frame_data, size_t frame_size)
{
  struct jpeg_decoder* decoder = (struct jpeg_decoder*) decoder_raw;

  const uint8_t* input_data = (const uint8_t*)frame_data;

  decoder->data.insert(decoder->data.end(), input_data, input_data + frame_size);

  struct heif_error err = {heif_error_Ok, heif_suberror_Unspecified, kSuccess};
  return err;
}


struct heif_error jpeg_decode_image(void* decoder_raw, struct heif_image** out_img)
{
  struct jpeg_decoder* decoder = (struct jpeg_decoder*) decoder_raw;


  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // to store embedded icc profile
//  uint32_t iccLen;
//  uint8_t* iccBuffer = NULL;

//  std::vector<uint8_t> xmpData;
//  std::vector<uint8_t> exifData;

  // initialize decompressor

  jpeg_create_decompress(&cinfo);

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_mem_src(&cinfo, decoder->data.data(), static_cast<unsigned long>(decoder->data.size()));

  /* Adding this part to prepare for icc profile reading. */
//  jpeg_save_markers(&cinfo, JPEG_ICC_MARKER, 0xFFFF);
//  jpeg_save_markers(&cinfo, JPEG_XMP_MARKER, 0xFFFF);
//  jpeg_save_markers(&cinfo, JPEG_EXIF_MARKER, 0xFFFF);

  jpeg_read_header(&cinfo, TRUE);

//  bool embeddedIccFlag = ReadICCProfileFromJPEG(&cinfo, &iccBuffer, &iccLen);
//  bool embeddedXMPFlag = ReadXMPFromJPEG(&cinfo, xmpData);
//  if (embeddedXMPFlag) {
//    img.xmp = xmpData;
//  }

//  bool embeddedEXIFFlag = ReadEXIFFromJPEG(&cinfo, exifData);
//  if (embeddedEXIFFlag) {
//    img.exif = exifData;
//    img.orientation = (heif_orientation) read_exif_orientation_tag(exifData.data(), (int) exifData.size());
//  }

  if (cinfo.jpeg_color_space == JCS_GRAYSCALE) {
    cinfo.out_color_space = JCS_GRAYSCALE;

    jpeg_start_decompress(&cinfo);

    JSAMPARRAY buffer;
    buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


    // create destination image

    struct heif_image* heif_img = nullptr;
    struct heif_error err = heif_image_create(cinfo.output_width, cinfo.output_height,
                                              heif_colorspace_monochrome,
                                              heif_chroma_monochrome,
                                              &heif_img);
    if (err.code != heif_error_Ok) {
      assert(heif_img==nullptr);
      return err;
    }

    heif_image_add_plane(heif_img, heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);

    int y_stride;
    uint8_t* py = heif_image_get_plane(heif_img, heif_channel_Y, &y_stride);


    // read the image

    while (cinfo.output_scanline < cinfo.output_height) {
      (void) jpeg_read_scanlines(&cinfo, buffer, 1);

      memcpy(py + (cinfo.output_scanline - 1) * y_stride, *buffer, cinfo.output_width);
    }

    *out_img = heif_img;
  }
  else {
    cinfo.out_color_space = JCS_YCbCr;

    jpeg_start_decompress(&cinfo);

    JSAMPARRAY buffer;
    buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


    // create destination image

    struct heif_image* heif_img = nullptr;
    struct heif_error err = heif_image_create(cinfo.output_width, cinfo.output_height,
                                              heif_colorspace_YCbCr,
                                              heif_chroma_420,
                                              &heif_img);
    if (err.code != heif_error_Ok) {
      assert(heif_img==nullptr);
      return err;
    }

    heif_image_add_plane(heif_img, heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);
    heif_image_add_plane(heif_img, heif_channel_Cb, (cinfo.output_width + 1) / 2, (cinfo.output_height + 1) / 2, 8);
    heif_image_add_plane(heif_img, heif_channel_Cr, (cinfo.output_width + 1) / 2, (cinfo.output_height + 1) / 2, 8);

    int y_stride;
    int cb_stride;
    int cr_stride;
    uint8_t* py = heif_image_get_plane(heif_img, heif_channel_Y, &y_stride);
    uint8_t* pcb = heif_image_get_plane(heif_img, heif_channel_Cb, &cb_stride);
    uint8_t* pcr = heif_image_get_plane(heif_img, heif_channel_Cr, &cr_stride);

    // read the image

    //printf("jpeg size: %d %d\n",cinfo.output_width, cinfo.output_height);

    while (cinfo.output_scanline < cinfo.output_height) {
      JOCTET* bufp;

      (void) jpeg_read_scanlines(&cinfo, buffer, 1);

      bufp = buffer[0];

      int y = cinfo.output_scanline - 1;

      for (unsigned int x = 0; x < cinfo.output_width; x += 2) {
        py[y * y_stride + x] = *bufp++;
        pcb[y / 2 * cb_stride + x / 2] = *bufp++;
        pcr[y / 2 * cr_stride + x / 2] = *bufp++;

        if (x + 1 < cinfo.output_width) {
          py[y * y_stride + x + 1] = *bufp++;
        }

        bufp += 2;
      }


      if (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        bufp = buffer[0];

        y = cinfo.output_scanline - 1;

        for (unsigned int x = 0; x < cinfo.output_width; x++) {
          py[y * y_stride + x] = *bufp++;
          bufp += 2;
        }
      }
    }

    *out_img = heif_img;
  }

//  if (embeddedIccFlag && iccLen > 0) {
//    heif_image_set_raw_color_profile(image, "prof", iccBuffer, (size_t) iccLen);
//  }

  // cleanup
//  free(iccBuffer);
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  decoder->data.clear();

  return heif_error_ok;
}


static const struct heif_decoder_plugin decoder_jpeg
    {
        3,
        jpeg_plugin_name,
        jpeg_init_plugin,
        jpeg_deinit_plugin,
        jpeg_does_support_format,
        jpeg_new_decoder,
        jpeg_free_decoder,
        jpeg_push_data,
        jpeg_decode_image,
        jpeg_set_strict_decoding,
        "jpeg"
    };


const struct heif_decoder_plugin* get_decoder_plugin_jpeg()
{
  return &decoder_jpeg;
}


#if PLUGIN_JPEG_DECODER
heif_plugin_info plugin_info {
  1,
  heif_plugin_type_decoder,
  &decoder_jpeg
};
#endif
