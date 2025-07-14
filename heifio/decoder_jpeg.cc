/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>

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

#include <memory>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <array>
#include "decoder_jpeg.h"
#include "exif.h"

extern "C" {
// Prevent duplicate definition for libjpeg-turbo v2.0
// Note: these 'undef's are only a workaround for a libjpeg-turbo-v2.0 bug and
// should be removed again later. Bug has been fixed in libjpeg-turbo-v2.0.1.
#include <jconfig.h>
#if defined(LIBJPEG_TURBO_VERSION_NUMBER) && LIBJPEG_TURBO_VERSION_NUMBER == 2000000
#undef HAVE_STDDEF_H
#undef HAVE_STDLIB_H
#endif
#include <jpeglib.h>
}

#define JPEG_EXIF_MARKER  (JPEG_APP0+1)  /* JPEG marker code for EXIF */
#define JPEG_EXIF_MARKER_LEN 6 // "Exif/0/0"
#define JPEG_XMP_MARKER  (JPEG_APP0+1)  /* JPEG marker code for XMP */
#define JPEG_XMP_MARKER_ID "http://ns.adobe.com/xap/1.0/"
#define JPEG_ICC_MARKER  (JPEG_APP0+2)  /* JPEG marker code for ICC */
#define JPEG_ICC_OVERHEAD_LEN  14        /* size of non-profile data in APP2 */

static struct heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

static bool JPEGMarkerIsIcc(jpeg_saved_marker_ptr marker)
{
  return
      marker->marker == JPEG_ICC_MARKER &&
      marker->data_length >= JPEG_ICC_OVERHEAD_LEN &&
      /* verify the identifying string */
      GETJOCTET(marker->data[0]) == 0x49 &&
      GETJOCTET(marker->data[1]) == 0x43 &&
      GETJOCTET(marker->data[2]) == 0x43 &&
      GETJOCTET(marker->data[3]) == 0x5F &&
      GETJOCTET(marker->data[4]) == 0x50 &&
      GETJOCTET(marker->data[5]) == 0x52 &&
      GETJOCTET(marker->data[6]) == 0x4F &&
      GETJOCTET(marker->data[7]) == 0x46 &&
      GETJOCTET(marker->data[8]) == 0x49 &&
      GETJOCTET(marker->data[9]) == 0x4C &&
      GETJOCTET(marker->data[10]) == 0x45 &&
      GETJOCTET(marker->data[11]) == 0x0;
}

bool ReadICCProfileFromJPEG(j_decompress_ptr cinfo,
                            JOCTET** icc_data_ptr,
                            unsigned int* icc_data_len)
{
  jpeg_saved_marker_ptr marker;
  int num_markers = 0;
  int seq_no;
  JOCTET* icc_data;
  unsigned int total_length;
#define MAX_SEQ_NO  255        /* sufficient since marker numbers are bytes */
  char marker_present[MAX_SEQ_NO + 1];      /* 1 if marker found */
  unsigned int data_length[MAX_SEQ_NO + 1]; /* size of profile data in marker */
  unsigned int data_offset[MAX_SEQ_NO + 1]; /* offset for data in marker */

  *icc_data_ptr = NULL;        /* avoid confusion if FALSE return */
  *icc_data_len = 0;

  /* This first pass over the saved markers discovers whether there are
   * any ICC markers and verifies the consistency of the marker numbering.
   */

  for (seq_no = 1; seq_no <= MAX_SEQ_NO; seq_no++)
    marker_present[seq_no] = 0;

  for (marker = cinfo->marker_list; marker != NULL; marker = marker->next) {
    if (JPEGMarkerIsIcc(marker)) {
      if (num_markers == 0)
        num_markers = GETJOCTET(marker->data[13]);
      else if (num_markers != GETJOCTET(marker->data[13]))
        return FALSE;        /* inconsistent num_markers fields */
      seq_no = GETJOCTET(marker->data[12]);
      if (seq_no <= 0 || seq_no > num_markers)
        return FALSE;        /* bogus sequence number */
      if (marker_present[seq_no])
        return FALSE;        /* duplicate sequence numbers */
      marker_present[seq_no] = 1;
      data_length[seq_no] = marker->data_length - JPEG_ICC_OVERHEAD_LEN;
    }
  }

  if (num_markers == 0)
    return FALSE;

  /* Check for missing markers, count total space needed,
   * compute offset of each marker's part of the data.
   */

  total_length = 0;
  for (seq_no = 1; seq_no <= num_markers; seq_no++) {
    if (marker_present[seq_no] == 0)
      return FALSE;        /* missing sequence number */
    data_offset[seq_no] = total_length;
    total_length += data_length[seq_no];
  }

  if (total_length <= 0)
    return FALSE;        /* found only empty markers? */

  /* Allocate space for assembled data */
  icc_data = (JOCTET*) malloc(total_length * sizeof(JOCTET));
  if (icc_data == NULL)
    return FALSE;        /* oops, out of memory */

  /* and fill it in */
  for (marker = cinfo->marker_list; marker != NULL; marker = marker->next) {
    if (JPEGMarkerIsIcc(marker)) {
      JOCTET FAR* src_ptr;
      JOCTET* dst_ptr;
      unsigned int length;
      seq_no = GETJOCTET(marker->data[12]);
      dst_ptr = icc_data + data_offset[seq_no];
      src_ptr = marker->data + JPEG_ICC_OVERHEAD_LEN;
      length = data_length[seq_no];
      while (length--) {
        *dst_ptr++ = *src_ptr++;
      }
    }
  }

  *icc_data_ptr = icc_data;
  *icc_data_len = total_length;

  return TRUE;
}


static bool JPEGMarkerIsXMP(jpeg_saved_marker_ptr marker)
{
  return
      marker->marker == JPEG_XMP_MARKER &&
      marker->data_length >= strlen(JPEG_XMP_MARKER_ID) + 1 &&
      strncmp((const char*) (marker->data), JPEG_XMP_MARKER_ID, strlen(JPEG_XMP_MARKER_ID)) == 0;
}

bool ReadXMPFromJPEG(j_decompress_ptr cinfo,
                     std::vector<uint8_t>& xmpData)
{
  jpeg_saved_marker_ptr marker;

  for (marker = cinfo->marker_list; marker != NULL; marker = marker->next) {
    if (JPEGMarkerIsXMP(marker)) {
      int length = (int) (marker->data_length - (strlen(JPEG_XMP_MARKER_ID) + 1));
      xmpData.resize(length);
      memcpy(xmpData.data(), marker->data + strlen(JPEG_XMP_MARKER_ID) + 1, length);
      return true;
    }
  }

  return false;
}


static bool JPEGMarkerIsEXIF(jpeg_saved_marker_ptr marker)
{
  return marker->marker == JPEG_EXIF_MARKER &&
         marker->data_length >= JPEG_EXIF_MARKER_LEN &&
         GETJOCTET(marker->data[0]) == 'E' &&
         GETJOCTET(marker->data[1]) == 'x' &&
         GETJOCTET(marker->data[2]) == 'i' &&
         GETJOCTET(marker->data[3]) == 'f' &&
         GETJOCTET(marker->data[4]) == 0 &&
         GETJOCTET(marker->data[5]) == 0;
}

bool ReadEXIFFromJPEG(j_decompress_ptr cinfo,
                      std::vector<uint8_t>& exifData)
{
  jpeg_saved_marker_ptr marker;

  for (marker = cinfo->marker_list; marker != NULL; marker = marker->next) {
    if (JPEGMarkerIsEXIF(marker)) {
      int length = (int) (marker->data_length - JPEG_EXIF_MARKER_LEN);
      exifData.resize(length);
      memcpy(exifData.data(), marker->data + JPEG_EXIF_MARKER_LEN, length);
      return true;
    }
  }

  return false;
}


#if JPEG_LIB_VERSION < 70
#define DCT_h_scaled_size DCT_scaled_size
#define DCT_v_scaled_size DCT_scaled_size
#endif


heif_error loadJPEG(const char *filename, InputImage *input_image)
{
  struct heif_image* image = nullptr;
  struct heif_error err = heif_error_ok;

  // ### Code copied from LibVideoGfx and slightly modified to use HeifPixelImage

  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // to store embedded icc profile
  uint32_t iccLen;
  uint8_t* iccBuffer = NULL;

  std::vector<uint8_t> xmpData;
  std::vector<uint8_t> exifData;

  // open input file

  FILE* infile;
  if ((infile = fopen(filename, "rb")) == NULL) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot open JPEG file"};
    return err;
  }


  // initialize decompressor

  jpeg_create_decompress(&cinfo);

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_stdio_src(&cinfo, infile);

  /* Adding this part to prepare for icc profile reading. */
  jpeg_save_markers(&cinfo, JPEG_ICC_MARKER, 0xFFFF);
  jpeg_save_markers(&cinfo, JPEG_XMP_MARKER, 0xFFFF);
  jpeg_save_markers(&cinfo, JPEG_EXIF_MARKER, 0xFFFF);

  jpeg_read_header(&cinfo, TRUE);

  bool embeddedIccFlag = ReadICCProfileFromJPEG(&cinfo, &iccBuffer, &iccLen);
  bool embeddedXMPFlag = ReadXMPFromJPEG(&cinfo, xmpData);
  if (embeddedXMPFlag) {
    input_image->xmp = xmpData;
  }

  bool embeddedEXIFFlag = ReadEXIFFromJPEG(&cinfo, exifData);
  if (embeddedEXIFFlag) {
    input_image->exif = exifData;
    input_image->orientation = (heif_orientation) read_exif_orientation_tag(exifData.data(), (int) exifData.size());
  }

  if (cinfo.jpeg_color_space == JCS_GRAYSCALE) {
    cinfo.out_color_space = JCS_GRAYSCALE;

    jpeg_start_decompress(&cinfo);

    JSAMPARRAY buffer;
    buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


    // create destination image

    err = heif_image_create(cinfo.output_width, cinfo.output_height,
                            heif_colorspace_monochrome,
                            heif_chroma_monochrome,
                            &image);
    if (err.code) {
      goto cleanup;
    }

    err = heif_image_add_plane(image, heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);
    if (err.code) { goto cleanup; }

    size_t y_stride;
    uint8_t* py = heif_image_get_plane2(image, heif_channel_Y, &y_stride);


    // read the image

    while (cinfo.output_scanline < cinfo.output_height) {
      (void) jpeg_read_scanlines(&cinfo, buffer, 1);

      memcpy(py + (cinfo.output_scanline - 1) * y_stride, *buffer, cinfo.output_width);
    }
  }
  else if (cinfo.jpeg_color_space == JCS_YCbCr) {
    cinfo.out_color_space = JCS_YCbCr;

    bool read_raw = false;
    heif_chroma output_chroma = heif_chroma_420;

    if (cinfo.comp_info[1].h_samp_factor == 1 &&
        cinfo.comp_info[1].v_samp_factor == 1 &&
        cinfo.comp_info[2].h_samp_factor == 1 &&
        cinfo.comp_info[2].v_samp_factor == 1) {

      if (cinfo.comp_info[0].h_samp_factor == 1 &&
          cinfo.comp_info[0].v_samp_factor == 1) {
        output_chroma = heif_chroma_444;
        read_raw = true;
      }
      else if (cinfo.comp_info[0].h_samp_factor == 2 &&
               cinfo.comp_info[0].v_samp_factor == 1) {
        output_chroma = heif_chroma_422;
        read_raw = true;
      }
      else if (cinfo.comp_info[0].h_samp_factor == 2 &&
               cinfo.comp_info[0].v_samp_factor == 2) {
        output_chroma = heif_chroma_420;
        read_raw = true;
      }
    }

    int cw=0,ch=0;
    switch (output_chroma) {
      case heif_chroma_420:
        cw = (cinfo.image_width + 1) / 2;
        ch = (cinfo.image_height + 1) / 2;
        break;
      case heif_chroma_422:
        cw = (cinfo.image_width + 1) / 2;
        ch = cinfo.image_height;
        break;
      case heif_chroma_444:
        cw = cinfo.image_width;
        ch = cinfo.image_height;
        break;
      default:
        assert(false);
    }

    //read_raw = false;

    cinfo.raw_data_out = boolean(read_raw);

    jpeg_start_decompress(&cinfo);


    // create destination image

    struct heif_error err = heif_image_create(cinfo.output_width, cinfo.output_height,
                                              heif_colorspace_YCbCr,
                                              output_chroma,
                                              &image);
    if (err.code) { goto cleanup; }

    err = heif_image_add_plane(image, heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);
    if (err.code) { goto cleanup; }

    err = heif_image_add_plane(image, heif_channel_Cb, cw, ch, 8);
    if (err.code) { goto cleanup; }

    err = heif_image_add_plane(image, heif_channel_Cr, cw, ch, 8);
    if (err.code) { goto cleanup; }

    size_t stride[3];
    uint8_t* p[3];
    p[0] = heif_image_get_plane2(image, heif_channel_Y, &stride[0]);
    p[1] = heif_image_get_plane2(image, heif_channel_Cb, &stride[1]);
    p[2] = heif_image_get_plane2(image, heif_channel_Cr, &stride[2]);

    // read the image

    if (read_raw) {
      // adapted from https://github.com/AOMediaCodec/libavif/blob/430ea2df584dcb95ff1632c17643ebbbb2f3bc81/apps/shared/avifjpeg.c

      JSAMPIMAGE buffer;
      buffer = (JSAMPIMAGE)(cinfo.mem->alloc_small)((j_common_ptr)&cinfo, JPOOL_IMAGE, sizeof(JSAMPARRAY) * cinfo.num_components);

      // lines of output image to be read per jpeg_read_raw_data call
      int readLines = 0;
      // lines of samples to be read per call (for each channel)
      int linesPerCall[3] = { 0, 0, 0 };
      // expected count of sample lines (for each channel)
      int targetRead[3] = { 0, 0, 0 };
      for (int i = 0; i < cinfo.num_components; ++i) {
        jpeg_component_info * comp = &cinfo.comp_info[i];

        linesPerCall[i] = comp->v_samp_factor * comp->DCT_v_scaled_size;
        targetRead[i] = comp->downsampled_height;
        buffer[i] = (cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                                                JPOOL_IMAGE,
                                                comp->width_in_blocks * comp->DCT_h_scaled_size,
                                                linesPerCall[i]);
        readLines = std::max(readLines, linesPerCall[i]);
      }

      // count of already-read lines (for each channel)
      int alreadyRead[3] = { 0, 0, 0 };
      int width[3] = { (int)cinfo.output_width, cw, cw};

      std::array<int,3> targetChannel{0,1,2};

      if (cinfo.jpeg_color_space == JCS_RGB) {
        targetChannel = {2, 0, 1};
      }
      else if (cinfo.jpeg_color_space == JCS_YCbCr ||
               cinfo.jpeg_color_space == JCS_GRAYSCALE) {
        targetChannel = {0, 1, 2};
      }
      else {
        return heif_error{heif_error_Unsupported_filetype,
                          heif_suberror_Unsupported_image_type,
                          "JPEG with unsupported colorspace"};
      }

      while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_raw_data(&cinfo, buffer, readLines);

        int workComponents = 3;

        for (int i = 0; i < workComponents; ++i) {
          int linesRead = std::min(targetRead[i] - alreadyRead[i], linesPerCall[i]);

          for (int j = 0; j < linesRead; ++j) {
            memcpy(p[targetChannel[i]] + ((size_t)stride[targetChannel[i]]) * (alreadyRead[i] + j),
                   buffer[i][j],
                   width[targetChannel[i]]);
          }
          alreadyRead[i] += linesPerCall[i];
        }
      }
    }
    else {
      JSAMPARRAY buffer;
      buffer = (*cinfo.mem->alloc_sarray)
          ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);

      while (cinfo.output_scanline < cinfo.output_height) {
        JOCTET* bufp;

        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        bufp = buffer[0];

        size_t y = cinfo.output_scanline - 1;

        for (unsigned int x = 0; x < cinfo.output_width; x += 2) {
          p[0][y * stride[0] + x] = *bufp++;
          p[1][y / 2 * stride[1] + x / 2] = *bufp++;
          p[2][y / 2 * stride[2] + x / 2] = *bufp++;

          if (x + 1 < cinfo.output_width) {
            p[0][y * stride[0] + x + 1] = *bufp++;
          }

          bufp += 2;
        }


        if (cinfo.output_scanline < cinfo.output_height) {
          (void) jpeg_read_scanlines(&cinfo, buffer, 1);

          bufp = buffer[0];

          y = cinfo.output_scanline - 1;

          for (unsigned int x = 0; x < cinfo.output_width; x++) {
            p[0][y * stride[0] + x] = *bufp++;
            bufp += 2;
          }
        }
      }
    }
  }
  else {
    // TODO: error, unsupported JPEG colorspace
  }

  if (embeddedIccFlag && iccLen > 0) {
    heif_image_set_raw_color_profile(image, "prof", iccBuffer, (size_t) iccLen);
  }

  input_image->image = std::shared_ptr<heif_image>(image,
                                                   [](heif_image* img) { heif_image_release(img); });

  // cleanup
cleanup:
  free(iccBuffer);
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  fclose(infile);

  return err;
}
