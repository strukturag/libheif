/*
  libheif unit tests

  MIT License

  Copyright (c) 2026 Junki Lee

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

#include "catch_amalgamated.hpp"

#include <libheif/heif.h>
#include <libheif/heif_plugin.h>

namespace {

int free_encoder_calls = 0;

const char* get_plugin_name()
{
  return "failing encoder test plugin";
}

heif_error failing_new_encoder(void** encoder)
{
  *encoder = new int(1);
  return {
    heif_error_Encoder_plugin_error,
    heif_suberror_Unspecified,
    "intentional encoder initialization failure"
  };
}

void free_encoder(void* encoder)
{
  delete static_cast<int*>(encoder);
  ++free_encoder_calls;
}

heif_encoder_plugin make_failing_plugin()
{
  heif_encoder_plugin plugin{};
  plugin.plugin_api_version = heif_encoder_plugin_latest_version;
  plugin.compression_format = heif_compression_JPEG;
  plugin.id_name = "failing-encoder-test-plugin";
  plugin.priority = 10000;
  plugin.supports_lossy_compression = 1;
  plugin.supports_lossless_compression = 0;
  plugin.get_plugin_name = get_plugin_name;
  plugin.new_encoder = failing_new_encoder;
  plugin.free_encoder = free_encoder;
  return plugin;
}

}  // namespace

TEST_CASE("failed encoder plugin initialization is cleaned up")
{
  static const heif_encoder_plugin plugin = make_failing_plugin();

  heif_error err = heif_register_encoder_plugin(&plugin);
  REQUIRE(err.code == heif_error_Ok);

  const heif_encoder_descriptor* descriptor = nullptr;
  REQUIRE(heif_get_encoder_descriptors(heif_compression_JPEG,
                                       plugin.id_name,
                                       &descriptor,
                                       1) == 1);

  int frees_before = free_encoder_calls;
  heif_encoder* encoder = nullptr;
  err = heif_context_get_encoder(nullptr, descriptor, &encoder);
  CHECK(err.code == heif_error_Encoder_plugin_error);
  CHECK(free_encoder_calls == frees_before + 1);
  CHECK(encoder == nullptr);
  if (encoder != nullptr) {
    heif_encoder_release(encoder);
  }

  frees_before = free_encoder_calls;
  encoder = nullptr;
  err = heif_context_get_encoder_for_format(nullptr,
                                            heif_compression_JPEG,
                                            &encoder);
  CHECK(err.code == heif_error_Encoder_plugin_error);
  CHECK(free_encoder_calls == frees_before + 1);
  CHECK(encoder == nullptr);
  if (encoder != nullptr) {
    heif_encoder_release(encoder);
  }
}
