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

#include "sdl.hh"
#include <assert.h>


bool SDL_YUV_Display::init(int frame_width, int frame_height, enum SDL_Chroma chroma,
                           const char* window_title)
{
  // reduce image size to a multiple of 8 (apparently required by YUV overlay)

  frame_width  &= ~7;
  frame_height &= ~7;

  mChroma = chroma;

  if (SDL_Init(SDL_INIT_VIDEO) < 0 ) {
    printf("SDL_Init() failed: %s\n", SDL_GetError( ) );
    SDL_Quit();
    return false;
  }

  // set window title
  mWindow = SDL_CreateWindow(window_title,
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    frame_width, frame_height, 0);
  if (!mWindow) {
    printf("SDL: Couldn't set video mode to %dx%d: %s\n",
           frame_width, frame_height, SDL_GetError());
    SDL_Quit();
    return false;
  }

  Uint32 flags = 0;  // Empty flags prioritize SDL_RENDERER_ACCELERATED.
  mRenderer = SDL_CreateRenderer(mWindow, -1, flags);
  if (!mRenderer) {
    printf("SDL: Couldn't create renderer: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  Uint32 pixelFormat = 0;
  switch (mChroma) {
  case SDL_CHROMA_MONO: pixelFormat = SDL_PIXELFORMAT_YV12; break;
  case SDL_CHROMA_420:  pixelFormat = SDL_PIXELFORMAT_YV12; break;
  case SDL_CHROMA_422:  pixelFormat = SDL_PIXELFORMAT_YV12; break;
  case SDL_CHROMA_444:  pixelFormat = SDL_PIXELFORMAT_YV12; break;
  //case SDL_CHROMA_444:  pixelFormat = SDL_PIXELFORMAT_YV12; break;
  default:
    printf("Unsupported chroma: %d\n", mChroma);
    SDL_Quit();
    return false;
  }

  mTexture = SDL_CreateTexture(mRenderer, pixelFormat,
    SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);
  if (!mTexture ) {
    printf("SDL: Couldn't create SDL texture: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  rect.x = 0;
  rect.y = 0;
  rect.w = frame_width;
  rect.h = frame_height;

  mWindowOpen=true;

  return true;
}

void SDL_YUV_Display::display(const unsigned char *Y,
                              const unsigned char *U,
                              const unsigned char *V,
                              int stride, int chroma_stride)
{
  if (!mWindowOpen) return;
  if (SDL_LockTexture(mTexture, nullptr,
    reinterpret_cast<void**>(&mPixels), &mStride) < 0) return;

  if (mChroma == SDL_CHROMA_420) {
    display420(Y,U,V,stride,chroma_stride);
  }
  else if (mChroma == SDL_CHROMA_422) {
    display422(Y,U,V,stride,chroma_stride);
  }
  else if (mChroma == SDL_CHROMA_444) {
    display444as420(Y,U,V,stride,chroma_stride);
    //display444as422(Y,U,V,stride,chroma_stride);
  }
  else if (mChroma == SDL_CHROMA_MONO) {
    display400(Y,stride);
  }

  SDL_UnlockTexture(mTexture);

  SDL_RenderCopy(mRenderer, mTexture, nullptr, nullptr);
  SDL_RenderPresent(mRenderer);
}


void SDL_YUV_Display::display420(const unsigned char *Y,
                                 const unsigned char *U,
                                 const unsigned char *V,
                                 int stride, int chroma_stride)
{
  if (stride == mStride && chroma_stride == mStride/2) {

    // fast copy

    memcpy(mPixels, Y, rect.w * rect.h);
    memcpy(&mPixels[rect.w * rect.h], V, rect.w * rect.h / 4);
    memcpy(&mPixels[(rect.w * rect.h) + (rect.w * rect.h / 4)], U, rect.w * rect.h / 4);
  }
  else {
    // copy line by line, because sizes are different
    uint8_t *dest = mPixels;

    for (int y=0;y<rect.h;y++,dest+=mStride)
      {
        memcpy(dest, Y+stride*y, rect.w);
      }

    for (int y=0;y<rect.h/2;y++,dest+=mStride/2)
      {
        memcpy(dest, V+chroma_stride*y, rect.w/2);
      }

    for (int y=0;y<rect.h/2;y++,dest+=mStride/2)
      {
        memcpy(dest, U+chroma_stride*y, rect.w/2);
      }
  }
}


void SDL_YUV_Display::display400(const unsigned char *Y, int stride)
{
  uint8_t *dest = mPixels;
  if (stride == mStride) {

    // fast copy

    memcpy(mPixels, Y, rect.w * rect.h);
    dest += mStride * rect.h;
  }
  else {
    // copy line by line, because sizes are different

    for (int y=0;y<rect.h;y++,dest+=mStride)
      {
        memcpy(dest, Y+stride*y, rect.w);
      }
  }

  // clear chroma planes

  memset(dest, 0x80, mStride * rect.h / 2);
}


void SDL_YUV_Display::display422(const unsigned char* Y,
                                 const unsigned char* U,
                                 const unsigned char* V,
                                 int stride, int chroma_stride)
{
  for (int y = 0; y < rect.h; y++) {
    unsigned char* dstY = mPixels + y * mStride;
    const unsigned char* Yp = Y + y * stride;

    memcpy(dstY, Yp, rect.w);
  }

  for (int y = 0; y < rect.h; y += 2) {
    unsigned char* dstV = mPixels + (y / 2) * mStride / 2 + rect.w * rect.h;
    unsigned char* dstU = mPixels + (y / 2) * mStride / 2 + rect.w * rect.h + rect.w * rect.h / 4;

    const unsigned char* Up = U + y * chroma_stride;
    const unsigned char* Vp = V + y * chroma_stride;

    memcpy(dstU, Up, rect.w / 2);
    memcpy(dstV, Vp, rect.w / 2);
  }
}


/* This converts down 4:4:4 input to 4:2:2 for display, as SDL does not support
   any 4:4:4 pixel format.
 */
void SDL_YUV_Display::display444as422(const unsigned char *Y,
                                      const unsigned char *U,
                                      const unsigned char *V,
                                      int stride, int chroma_stride)
{
  for (int y=0;y<rect.h;y++)
    {
      unsigned char* p = mPixels + y*mStride *2;

      const unsigned char* Yp = Y + y*stride;
      const unsigned char* Up = U + y*chroma_stride;
      const unsigned char* Vp = V + y*chroma_stride;

      for (int x=0;x<rect.w;x+=2) {
        *p++ = Yp[x];
        *p++ = Up[x];
        *p++ = Yp[x+1];
        *p++ = Vp[x];
      }
    }
}


void SDL_YUV_Display::display444as420(const unsigned char *Y,
                                      const unsigned char *U,
                                      const unsigned char *V,
                                      int stride, int chroma_stride)
{
  for (int y=0;y<rect.h;y++)
    {
      unsigned char* p = mPixels + y*mStride;
      memcpy(p, Y+y*stride, rect.w);
    }

  uint8_t *startV = mPixels + (rect.h*mStride);
  uint8_t *startU = startV + (rect.h*mStride/2);
  for (int y=0;y<rect.h;y+=2)
    {
      uint8_t* u = startU + y/2*mStride/2;
      uint8_t* v = startV + y/2*mStride/2;

      for (int x=0;x<rect.w;x+=2) {
        u[x / 2] = static_cast<uint8_t>((U[(y + 0) * chroma_stride + x] + U[(y + 0) * chroma_stride + x + 1] +
                                         U[(y + 1) * chroma_stride + x] + U[(y + 1) * chroma_stride + x + 1]) / 4);
        v[x / 2] = static_cast<uint8_t>((V[(y + 0) * chroma_stride + x] + V[(y + 0) * chroma_stride + x + 1] +
                                         V[(y + 1) * chroma_stride + x] + V[(y + 1) * chroma_stride + x + 1]) / 4);

        //u[x/2] = U[y*chroma_stride + x];
        //v[x/2] = V[y*chroma_stride + x];
      }
    }
}


bool SDL_YUV_Display::doQuit() const
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      return true;
    }
  }

  return false;
}

void SDL_YUV_Display::close()
{
  if (mTexture) {
    SDL_DestroyTexture(mTexture);
    mTexture = nullptr;
  }
  if (mRenderer) {
    SDL_DestroyRenderer(mRenderer);
    mRenderer = nullptr;
  }
  if (mWindow) {
    SDL_DestroyWindow(mWindow);
    mWindow = nullptr;
  }
  SDL_Quit();

  mWindowOpen=false;
}
