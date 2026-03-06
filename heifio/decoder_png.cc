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

#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <memory>
#include "decoder_png.h"
#include "exif.h"

extern "C" {
#include <png.h>
}

static struct heif_error heif_error_ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

static void
user_read_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
  FILE* fh = (FILE*) png_get_io_ptr(png_ptr);
  size_t n = fread((char*) data, length, 1, fh);
  (void) n;
} // user_read_data


heif_error loadPNG(const char* filename, int output_bit_depth, InputImage *input_image)
{
  FILE* fh = fopen(filename, "rb");
  if (!fh) {
    struct heif_error err = {
      .code = heif_error_Invalid_input,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot open PNG file"};
    return err;
  }

  // ### Code copied from LibVideoGfx and slightly modified to use HeifPixelImage

  struct heif_image* image = nullptr;

  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type;
  int compression_type;
  png_charp name;
#if (PNG_LIBPNG_VER < 10500)
  png_charp png_profile_data;
#else
  png_bytep png_profile_data;
#endif
  uint8_t* profile_data = nullptr;
  png_uint_32 profile_length = 5;

  /* Create and initialize the png_struct with the desired error handler
   * functions.  If you want to use the default stderr and longjump method,
   * you can supply NULL for the last three parameters.  We also supply the
   * the compiler header file version, so that we know if the application
   * was compiled with a compatible version of the library.  REQUIRED
   */
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  assert(png_ptr != NULL);

  /* Allocate/initialize the memory for image information.  REQUIRED. */
  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL) {
    png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
    assert(false); // , "could not create info_ptr");
  } // if

  /* Set error handling if you are using the setjmp/longjmp method (this is
   * the normal method of doing things with libpng).  REQUIRED unless you
   * set up your own error handlers in the png_create_read_struct() earlier.
   */
  if (setjmp(png_jmpbuf(png_ptr))) {
    /* Free all of the memory associated with the png_ptr and info_ptr */
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);
    /* If we get here, we had a problem reading the file */
    assert(false); // , "fatal error in png library");
  } // if

  /* If you are using replacement read functions, instead of calling
   * png_init_io() here you would call: */
  png_set_read_fn(png_ptr, (void*) fh, user_read_fn);
  /* where user_io_ptr is a structure you want available to the callbacks */

  /* The call to png_read_info() gives us all of the information from the
   * PNG file before the first IDAT (image data chunk).  REQUIRED
   */
  png_read_info(png_ptr, info_ptr);

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               &interlace_type, NULL, NULL);

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP)) {
    if (PNG_INFO_iCCP ==
        png_get_iCCP(png_ptr, info_ptr, &name, &compression_type, &png_profile_data, &profile_length) &&
        profile_length > 0) {
      profile_data = (uint8_t*) malloc(profile_length);
      if (profile_data) {
        memcpy(profile_data, png_profile_data, profile_length);
      }
    }
  }
  /**** Set up the data transformations you want.  Note that these are all
   **** optional.  Only call them if you want/need them.  Many of the
   **** transformations only work on specific types of images, and many
   **** are mutually exclusive.
   ****/

  // \TODO
  //      /* Strip alpha bytes from the input data without combining with the
  //       * background (not recommended).
  //       */
  //      png_set_strip_alpha(png_ptr);

  /* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
   * byte into separate bytes (useful for paletted and grayscale images).
   */
  png_set_packing(png_ptr);


  /* Expand paletted colors into true RGB triplets */
  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png_ptr);
    bit_depth = 8;
  }

  /* Expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel */
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png_ptr);
    bit_depth = 8;
  }

  /* Set the background color to draw transparent and alpha images over.
   * It is possible to set the red, green, and blue components directly
   * for paletted images instead of supplying a palette index.  Note that
   * even if the PNG file supplies a background, you are not required to
   * use it - you should use the (solid) application background if it has one.
   */

#if 0
  // \TODO 0 is index in color lookup table - correct? used already?
  png_color_16 my_background = {0, 255, 255, 255, 255};
  png_color_16 *image_background;

  if (png_get_bKGD(png_ptr, info_ptr, &image_background))
    png_set_background(png_ptr, image_background, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
  else
    png_set_background(png_ptr, &my_background, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
#endif


  /* Optional call to gamma correct and add the background to the palette
   * and update info structure.  REQUIRED if you are expecting libpng to
   * update the palette for you (ie you selected such a transform above).
   */
  png_read_update_info(png_ptr, info_ptr);

  /* Allocate the memory to hold the image using the fields of info_ptr. */

  /* The easiest way to read the image: */
  uint8_t** row_pointers = new png_bytep[height];
  assert(row_pointers != NULL);

  for (uint32_t y = 0; y < height; y++) {
    row_pointers[y] = (png_bytep) malloc(png_get_rowbytes(png_ptr, info_ptr));
    assert(row_pointers[y] != NULL);
  } // for

  /* Now it's time to read the image.  One of these methods is REQUIRED */
  png_read_image(png_ptr, row_pointers);

  /* read rest of file, and get additional chunks in info_ptr - REQUIRED */
  png_read_end(png_ptr, info_ptr);


  // --- read EXIF data

#ifdef PNG_eXIf_SUPPORTED
  png_bytep exifPtr = nullptr;
  png_uint_32 exifSize = 0;
  if (png_get_eXIf_1(png_ptr, info_ptr, &exifSize, &exifPtr) == PNG_INFO_eXIf) {
    input_image->exif.resize(exifSize);
    memcpy(input_image->exif.data(), exifPtr, exifSize);

    // remove the EXIF orientation since it is informal only in PNG and we do not want to confuse with an orientation not matching irot/imir
    modify_exif_orientation_tag_if_it_exists(input_image->exif.data(), (int) input_image->exif.size(), 1);
  }
#endif

  // --- read XMP data

#ifdef PNG_iTXt_SUPPORTED
  png_textp textPtr = nullptr;
  const png_uint_32 nTextChunks = png_get_text(png_ptr, info_ptr, &textPtr, nullptr);
  for (png_uint_32 i = 0; i < nTextChunks; i++, textPtr++) {
    png_size_t textLength = textPtr->text_length;
    if ((textPtr->compression == PNG_ITXT_COMPRESSION_NONE) || (textPtr->compression == PNG_ITXT_COMPRESSION_zTXt)) {
      textLength = textPtr->itxt_length;
    }

    if (!strcmp(textPtr->key, "XML:com.adobe.xmp")) {
      if (textLength == 0) {
        // TODO: error
      }
      else {
        input_image->xmp.resize(textLength);
        memcpy(input_image->xmp.data(), textPtr->text, textLength);
      }
    }
  }
#endif

  int band = png_get_channels(png_ptr, info_ptr);

  /* clean up after the read, and free any memory allocated - REQUIRED */
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);


  struct heif_error err;

  bool has_alpha = (band == 2 || band == 4);

  if (band == 1 && bit_depth == 8) {
    err = heif_image_create((int) width, (int) height,
                            heif_colorspace_monochrome,
                            heif_chroma_monochrome,
                            &image);
    (void) err;

    heif_image_add_plane(image, heif_channel_Y, (int) width, (int) height, 8);

    size_t y_stride;
    size_t a_stride;
    uint8_t* py = heif_image_get_plane2(image, heif_channel_Y, &y_stride);
    uint8_t* pa = nullptr;

    if (has_alpha) {
      heif_image_add_plane(image, heif_channel_Alpha, (int) width, (int) height, 8);

      pa = heif_image_get_plane2(image, heif_channel_Alpha, &a_stride);
    }


    for (uint32_t y = 0; y < height; y++) {
      uint8_t* p = row_pointers[y];

      if (has_alpha) {
        for (uint32_t x = 0; x < width; x++) {
          py[y * y_stride + x] = *p++;
          pa[y * a_stride + x] = *p++;
        }
      }
      else {
        memcpy(&py[y * y_stride], p, width);
      }
    }
  }
  else if (band == 1) {
    assert(bit_depth > 8);

    err = heif_image_create((int) width, (int) height,
                            heif_colorspace_monochrome,
                            heif_chroma_monochrome,
                            &image);
    (void) err;

    int bdShift = 16 - output_bit_depth;

    heif_image_add_plane(image, heif_channel_Y, (int) width, (int) height, output_bit_depth);

    size_t y_stride;
    size_t a_stride = 0;
    uint16_t* py = (uint16_t*) heif_image_get_plane2(image, heif_channel_Y, &y_stride);
    uint16_t* pa = nullptr;

    if (has_alpha) {
      heif_image_add_plane(image, heif_channel_Alpha, (int) width, (int) height, output_bit_depth);

      pa = (uint16_t*) heif_image_get_plane2(image, heif_channel_Alpha, &a_stride);
    }

    y_stride /= 2;
    a_stride /= 2;

    for (uint32_t y = 0; y < height; y++) {
      uint8_t* p = row_pointers[y];

      if (has_alpha) {
        for (uint32_t x = 0; x < width; x++) {
          uint16_t vp = (uint16_t) (((p[0] << 8) | p[1]) >> bdShift);
          uint16_t va = (uint16_t) (((p[2] << 8) | p[3]) >> bdShift);

          py[x + y * y_stride] = vp;
          pa[x + y * y_stride] = va;

          p += 4;
        }
      }
      else {
        for (uint32_t x = 0; x < width; x++) {
          uint16_t vp = (uint16_t) (((p[0] << 8) | p[1]) >> bdShift);

          py[x + y * y_stride] = vp;

          p += 2;
        }
      }
    }
  }
  else if (band == 2 && bit_depth==8) {
    err = heif_image_create((int) width, (int) height,
                            heif_colorspace_monochrome,
                            heif_chroma_monochrome,
                            &image);
    (void) err;

    heif_image_add_plane(image, heif_channel_Y, (int) width, (int) height, 8);
    heif_image_add_plane(image, heif_channel_Alpha, (int) width, (int) height, 8);

    size_t stride;
    uint8_t* p = heif_image_get_plane2(image, heif_channel_Y, &stride);

    size_t strideA;
    uint8_t* pA = heif_image_get_plane2(image, heif_channel_Alpha, &strideA);

    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        p[y * stride + x] = row_pointers[y][2 * x];
        pA[y * strideA + x] = row_pointers[y][2 * x + 1];
      }
    }
  }
  else if (bit_depth == 8) {
    err = heif_image_create((int) width, (int) height,
                            heif_colorspace_RGB,
                            has_alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB,
                            &image);
    (void) err;

    heif_image_add_plane(image, heif_channel_interleaved, (int) width, (int) height,
                         has_alpha ? 32 : 24);

    size_t stride;
    uint8_t* p = heif_image_get_plane2(image, heif_channel_interleaved, &stride);

    for (uint32_t y = 0; y < height; y++) {
      if (has_alpha) {
        memcpy(p + y * stride, row_pointers[y], width * 4);
      }
      else {
        memcpy(p + y * stride, row_pointers[y], width * 3);
      }
    }
  }
  else {
    if (output_bit_depth == 8) {
      err = heif_image_create((int) width, (int) height,
                              heif_colorspace_RGB,
                              has_alpha ?
                              heif_chroma_interleaved_RGBA :
                              heif_chroma_interleaved_RGB,
                              &image);
    }
    else {
      err = heif_image_create((int) width, (int) height,
                              heif_colorspace_RGB,
                              has_alpha ?
                              heif_chroma_interleaved_RRGGBBAA_LE :
                              heif_chroma_interleaved_RRGGBB_LE,
                              &image);
    }
    (void) err;

    int bdShift = 16 - output_bit_depth;

    heif_image_add_plane(image, heif_channel_interleaved, (int) width, (int) height, output_bit_depth);

    size_t stride;
    uint8_t* p_out = (uint8_t*) heif_image_get_plane2(image, heif_channel_interleaved, &stride);

    if (output_bit_depth==8) {
      // convert HDR to SDR

      for (uint32_t y = 0; y < height; y++) {
        uint8_t* p = row_pointers[y];

        uint32_t nVal = (has_alpha ? 4 : 3) * width;

        for (uint32_t x = 0; x < nVal; x++) {
          p_out[x + y * stride] = p[0];
          p+=2;
        }
      }
    }
    else {
      for (uint32_t y = 0; y < height; y++) {
        uint8_t* p = row_pointers[y];

        uint32_t nVal = (has_alpha ? 4 : 3) * width;

        for (uint32_t x = 0; x < nVal; x++) {
          uint16_t v = (uint16_t) (((p[0] << 8) | p[1]) >> bdShift);
          p_out[2 * x + y * stride + 1] = (uint8_t) (v >> 8);
          p_out[2 * x + y * stride + 0] = (uint8_t) (v & 0xFF);
          p += 2;
        }
      }
    }
  }

  if (profile_data && profile_length > 0) {
    heif_image_set_raw_color_profile(image, "prof", profile_data, (size_t) profile_length);
  }

  free(profile_data);
  for (uint32_t y = 0; y < height; y++) {
    free(row_pointers[y]);
  } // for

  delete[] row_pointers;
  fclose(fh);

  input_image->image = std::shared_ptr<heif_image>(image,
                                                  [](heif_image* img) { heif_image_release(img); });

  return heif_error_ok;
}
