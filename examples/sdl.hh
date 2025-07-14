/*
  This file is part of dec265, an example application using libde265.

  MIT License

  Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>

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

#include <SDL.h>


class SDL_YUV_Display
{
public:

  enum SDL_Chroma {
    SDL_CHROMA_MONO=400,
    SDL_CHROMA_420 =420,
    SDL_CHROMA_422 =422,
    SDL_CHROMA_444 =444
  };

  bool init(int frame_width, int frame_height, enum SDL_Chroma chroma, const char* window_title);
  void display(const unsigned char *Y, const unsigned char *U, const unsigned char *V,
               int stride, int chroma_stride);
  void close();

  bool doQuit() const;

  bool isOpen() const { return mWindowOpen; }

private:
  SDL_Window *mWindow = nullptr;
  SDL_Renderer *mRenderer = nullptr;
  SDL_Texture *mTexture = nullptr;
  SDL_Rect     rect;
  bool         mWindowOpen;
  uint8_t *mPixels = nullptr;
  int mStride = 0;

  SDL_Chroma mChroma;

  void display400(const unsigned char *Y,
                  int stride);
  void display420(const unsigned char *Y,
                  const unsigned char *U,
                  const unsigned char *V,
                  int stride, int chroma_stride);
  void display422(const unsigned char *Y,
                  const unsigned char *U,
                  const unsigned char *V,
                  int stride, int chroma_stride);
  void display444as422(const unsigned char *Y,
                       const unsigned char *U,
                       const unsigned char *V,
                       int stride, int chroma_stride);
  void display444as420(const unsigned char *Y,
                       const unsigned char *U,
                       const unsigned char *V,
                       int stride, int chroma_stride);
};
