/*
  libheif example application "heif".

  MIT License

  Copyright (c) 2024 Dirk Farin <dirk.farin@gmail.com>

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

#include "libheif/heif.h"
#include "decoder.h"


#if !HAVE_LIBJPEG
heif_error loadJPEG(const char *filename, InputImage *input_image)
{
  struct heif_error err = {
      .code = heif_error_Unsupported_feature,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot load JPEG because libjpeg support was not compiled."};
  return err;
}
#endif


#if !HAVE_LIBPNG
heif_error loadPNG(const char* filename, int output_bit_depth, InputImage *input_image)
{
  struct heif_error err = {
      .code = heif_error_Unsupported_feature,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot load PNG because libpng support was not compiled."};
  return err;
}
#endif


#if !HAVE_LIBTIFF
heif_error loadTIFF(const char *filename, InputImage *input_image)
{
  struct heif_error err = {
      .code = heif_error_Unsupported_feature,
      .subcode = heif_suberror_Unspecified,
      .message = "Cannot load TIFF because libtiff support was not compiled."};
  return err;
}
#endif
