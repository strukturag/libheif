/*
  libheif integration tests for the 'altt' accessibility text property.

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

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
#include "test_utils.h"
#include <libheif/heif.h>
#include <libheif/heif_properties.h>

#include <cstring>


TEST_CASE("accessibility text round trip")
{
  heif_error err{};

  err = heif_init(nullptr);
  REQUIRE(err.code == heif_error_Ok);

  std::string filename = get_tests_output_file_path("accessibility_text-1.heic");

  heif_image* img = createImage_RGB_planar();
  heif_encoder* enc = get_encoder_or_skip_test(heif_compression_HEVC);
  heif_context* ctx = heif_context_alloc();

  heif_image_handle* handle;
  err = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
  REQUIRE(err.code == heif_error_Ok);
  heif_item_id itemId = heif_image_handle_get_item_id(handle);

  // --- add accessibility texts in two languages

  heif_property_accessibility_text altt_en{};
  altt_en.version = 1;
  altt_en.alt_text = "a gray square";
  altt_en.alt_lang = "en-US";

  heif_property_id propertyId = 0;
  err = heif_item_add_property_accessibility_text(ctx, itemId, &altt_en, &propertyId);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(propertyId != 0);

  heif_property_accessibility_text altt_de{};
  altt_de.version = 1;
  altt_de.alt_text = "ein graues Quadrat";
  altt_de.alt_lang = "de-DE";

  err = heif_item_add_property_accessibility_text(ctx, itemId, &altt_de, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  // adding a second text with an already used language must fail

  heif_property_accessibility_text altt_dup{};
  altt_dup.version = 1;
  altt_dup.alt_text = "another text";
  altt_dup.alt_lang = "en-US";

  err = heif_item_add_property_accessibility_text(ctx, itemId, &altt_dup, nullptr);
  REQUIRE(err.code != heif_error_Ok);

  // NULL strings are mapped to empty strings (undefined language)

  heif_property_accessibility_text altt_undef{};
  altt_undef.version = 1;
  altt_undef.alt_text = "text with undefined language";
  altt_undef.alt_lang = nullptr;

  err = heif_item_add_property_accessibility_text(ctx, itemId, &altt_undef, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_write_to_file(ctx, filename.c_str());
  REQUIRE(err.code == heif_error_Ok);

  heif_image_handle_release(handle);
  heif_context_free(ctx);
  heif_image_release(img);


  // --- read file

  ctx = heif_context_alloc();
  err = heif_context_read_from_file(ctx, filename.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  err = heif_context_get_primary_image_handle(ctx, &handle);
  REQUIRE(err.code == heif_error_Ok);

  itemId = heif_image_handle_get_item_id(handle);

  // --- single-property getter

  heif_property_id propertyIds[10];
  int count = heif_item_get_properties_of_type(ctx, itemId, heif_item_property_type_accessibility_text,
                                               propertyIds, 10);
  REQUIRE(count == 3);

  heif_property_accessibility_text* altt = nullptr;
  err = heif_item_get_property_accessibility_text(ctx, itemId, propertyIds[0], &altt);
  REQUIRE(err.code == heif_error_Ok);
  REQUIRE(altt != nullptr);
  REQUIRE(std::string(altt->alt_text) == "a gray square");
  REQUIRE(std::string(altt->alt_lang) == "en-US");
  heif_property_accessibility_text_release(altt);

  // --- bulk getter with count

  int total = -1;
  heif_property_accessibility_text* texts = heif_item_get_accessibility_texts(ctx, itemId, &total);
  REQUIRE(texts != nullptr);
  REQUIRE(total == 3);

  REQUIRE(std::string(texts[0].alt_text) == "a gray square");
  REQUIRE(std::string(texts[0].alt_lang) == "en-US");
  REQUIRE(std::string(texts[1].alt_text) == "ein graues Quadrat");
  REQUIRE(std::string(texts[1].alt_lang) == "de-DE");
  REQUIRE(std::string(texts[2].alt_text) == "text with undefined language");
  REQUIRE(std::string(texts[2].alt_lang).empty());

  // the array is terminated by an element with NULL strings

  REQUIRE(texts[3].alt_text == nullptr);
  REQUIRE(texts[3].alt_lang == nullptr);

  heif_property_accessibility_text_array_release(texts);

  // --- bulk getter without count, iterating until the terminator

  texts = heif_item_get_accessibility_texts(ctx, itemId, nullptr);
  REQUIRE(texts != nullptr);

  int n = 0;
  for (int i = 0; texts[i].alt_text != nullptr; i++) {
    n++;
  }
  REQUIRE(n == 3);

  heif_property_accessibility_text_array_release(texts);

  // --- an item without altt properties yields an empty, terminated array

  texts = heif_item_get_accessibility_texts(ctx, 0xFFFF, &total);
  REQUIRE(texts != nullptr);
  REQUIRE(total == 0);
  REQUIRE(texts[0].alt_text == nullptr);
  REQUIRE(texts[0].alt_lang == nullptr);
  heif_property_accessibility_text_array_release(texts);

  // releasing NULL is allowed

  heif_property_accessibility_text_array_release(nullptr);

  heif_image_handle_release(handle);
  heif_context_free(ctx);

  heif_deinit();
}
