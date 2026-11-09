/*
  libheif tests for reading item data from truncated input.

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
#include "libheif/heif.h"
#include "libheif/heif_items.h"
#include "test_utils.h"
#include "test-config.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

// rainbow-451x461.heic: 7080 bytes, 'meta' ends at 1158,
// item 1 (hvc1) data is 1166..3540, item 2 (mime) data is 3540..7080
constexpr size_t fixture_size = 7080;
constexpr heif_item_id hevc_item = 1;
constexpr uint64_t hevc_item_end = 3540;
constexpr size_t hevc_item_size = 2374;
constexpr heif_item_id mime_item = 2;
constexpr uint64_t mime_item_end = 7080;
constexpr size_t mime_item_size = 3540;


std::vector<uint8_t> load_fixture()
{
  std::ifstream istr(tests_data_directory + "/rainbow-451x461.heic", std::ios::binary);
  REQUIRE(istr.good());
  std::vector<uint8_t> file{std::istreambuf_iterator<char>(istr), std::istreambuf_iterator<char>()};
  REQUIRE(file.size() == fixture_size);
  return file;
}


std::string expected_message(heif_item_id item_id, uint64_t needed_end, std::optional<uint64_t> available)
{
  std::string msg = "Invalid input: Unexpected end of file: Extent in iloc box references data outside of file bounds (item " +
                    std::to_string(item_id) + " needs data up to file position " + std::to_string(needed_end);
  if (available) {
    msg += ", but only " + std::to_string(*available) + " bytes are available";
  }
  return msg + "; the file may be truncated or still being written)";
}


struct ItemReadResult
{
  heif_error_code code;
  heif_suberror_code subcode;
  std::string message;
  size_t size;
};


ItemReadResult read_item(heif_context* ctx, heif_item_id id)
{
  uint8_t* data = nullptr;
  size_t size = 0;
  heif_error err = heif_item_get_item_data(ctx, id, nullptr, &data, &size);

  ItemReadResult result{err.code, err.subcode, err.message ? err.message : "", size};
  heif_release_item_data(ctx, &data);
  return result;
}


void check_truncation_error(const ItemReadResult& result, heif_item_id item_id, uint64_t needed_end, std::optional<uint64_t> available)
{
  INFO("message: " << result.message);
  CHECK(result.code == heif_error_Invalid_input);
  CHECK(result.subcode == heif_suberror_End_of_data);
  CHECK(result.message == expected_message(item_id, needed_end, available));
  CHECK(result.message.find('\n') == std::string::npos);
  CHECK(result.size == 0);
}


// A version-2 heif_reader on a memory buffer that counts request_range() and wait_for_file_size() calls.
struct CountingReader
{
  const std::vector<uint8_t>* data = nullptr;
  int64_t position = 0;
  int request_range_calls = 0;
  int wait_for_file_size_calls = 0;
};

int64_t reader_get_position(void* userdata)
{
  return static_cast<CountingReader*>(userdata)->position;
}

int reader_read(void* dst, size_t size, void* userdata)
{
  auto* reader = static_cast<CountingReader*>(userdata);
  auto read_end = reader->position + static_cast<int64_t>(size);
  if (read_end > static_cast<int64_t>(reader->data->size())) {
    return 1;
  }

  memcpy(dst, reader->data->data() + reader->position, size);
  reader->position = read_end;
  return 0;
}

int reader_seek(int64_t position, void* userdata)
{
  auto* reader = static_cast<CountingReader*>(userdata);
  if (position < 0 || position > static_cast<int64_t>(reader->data->size())) {
    return 1;
  }

  reader->position = position;
  return 0;
}

heif_reader_grow_status reader_wait_for_file_size(int64_t target_size, void* userdata)
{
  auto* reader = static_cast<CountingReader*>(userdata);
  reader->wait_for_file_size_calls++;
  return (target_size <= static_cast<int64_t>(reader->data->size()))
             ? heif_reader_grow_status_size_reached
             : heif_reader_grow_status_size_beyond_eof;
}

heif_reader_range_request_result reader_request_range(uint64_t start_pos, uint64_t end_pos, void* userdata)
{
  auto* reader = static_cast<CountingReader*>(userdata);
  reader->request_range_calls++;

  heif_reader_range_request_result result{};
  if (end_pos <= reader->data->size()) {
    result.status = heif_reader_grow_status_size_reached;
    result.range_end = end_pos;
  }
  else {
    result.status = heif_reader_grow_status_size_beyond_eof;
    result.range_end = reader->data->size();
  }
  return result;
}

void reader_preload_range_hint(uint64_t, uint64_t, void*) {}

void reader_release_file_range(uint64_t, uint64_t, void*) {}

heif_reader make_counting_reader_api()
{
  heif_reader reader{};
  reader.reader_api_version = 2;
  reader.get_position = reader_get_position;
  reader.read = reader_read;
  reader.seek = reader_seek;
  reader.wait_for_file_size = reader_wait_for_file_size;
  reader.request_range = reader_request_range;
  // called without a NULL check for version 2 readers
  reader.preload_range_hint = reader_preload_range_hint;
  reader.release_file_range = reader_release_file_range;
  return reader;
}

} // namespace


TEST_CASE("untruncated fixture reads both items")
{
  const std::vector<uint8_t> file = load_fixture();

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), file.size(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  ItemReadResult hevc = read_item(ctx, hevc_item);
  CHECK(hevc.code == heif_error_Ok);
  CHECK(hevc.size == hevc_item_size);

  ItemReadResult mime = read_item(ctx, mime_item);
  CHECK(mime.code == heif_error_Ok);
  CHECK(mime.size == mime_item_size);

  heif_context_free(ctx);
}


TEST_CASE("truncated memory input reports item, needed end and available size")
{
  const std::vector<uint8_t> file = load_fixture();
  const size_t truncated_size = 2000;

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error err = heif_context_read_from_memory_without_copy(ctx, file.data(), truncated_size, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  check_truncation_error(read_item(ctx, hevc_item), hevc_item, hevc_item_end, truncated_size);

  heif_context_free(ctx);
}


TEST_CASE("truncated file input reports item, needed end and available size")
{
  const std::vector<uint8_t> file = load_fixture();
  const size_t truncated_size = 2000;

  std::string path = get_tests_output_file_path("truncated_item_data-2000.heic");
  {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(truncated_size));
    REQUIRE(out.good());
  }

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
  REQUIRE(err.code == heif_error_Ok);

  check_truncation_error(read_item(ctx, hevc_item), hevc_item, hevc_item_end, truncated_size);

  heif_context_free(ctx);
}


TEST_CASE("truncated version-2 reader input reports item and needed end without probing the size")
{
  const std::vector<uint8_t> file = load_fixture();
  const size_t truncated_size = 5000;
  const std::vector<uint8_t> truncated(file.begin(), file.begin() + truncated_size);

  heif_reader reader_api = make_counting_reader_api();
  CountingReader reader;
  reader.data = &truncated;

  heif_context* ctx = heif_context_alloc();
  REQUIRE(ctx != nullptr);
  heif_error err = heif_context_read_from_reader(ctx, &reader_api, &reader, nullptr);
  REQUIRE(err.code == heif_error_Ok);

  reader.request_range_calls = 0;
  reader.wait_for_file_size_calls = 0;
  check_truncation_error(read_item(ctx, mime_item), mime_item, mime_item_end, std::nullopt);
  CHECK(reader.request_range_calls == 0);
  // only the bounds check itself, no size probing for the message
  CHECK(reader.wait_for_file_size_calls == 1);

  ItemReadResult hevc = read_item(ctx, hevc_item);
  CHECK(hevc.code == heif_error_Ok);
  CHECK(hevc.size == hevc_item_size);

  heif_context_free(ctx);
}
