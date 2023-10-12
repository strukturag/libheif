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

#include "common.h"
#include "libheif/heif.h"
#include <iostream>


void show_version()
{
  std::cout << LIBHEIF_VERSION << '\n'
            << "libheif: " << heif_get_version() << '\n';
  {
    auto paths = heif_get_plugin_directories();
    for (int i=0;paths[i];i++) {
      std::cout << "plugin path: " << paths[i] << '\n';
    }

    if (paths[0] == nullptr) {
      std::cout << "plugin path: plugins are disabled\n";
    }

    heif_free_plugin_directories(paths);
  }
}
