/*
  libheif integration tests: hand-built HEIF sequence files.

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

// Builds minimal, structurally valid HEIF sequence files following the box
// tree that Track::load() requires. Defaults to a 'urim' metadata track for
// raw-sample API tests; visual-track tests can override its sample entries.

#ifndef LIBHEIF_TESTS_SEQUENCE_FILE_BUILDER_H
#define LIBHEIF_TESTS_SEQUENCE_FILE_BUILDER_H

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace seqfile {

inline void put16(std::vector<uint8_t>& v, uint16_t x)
{
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}

inline void put32(std::vector<uint8_t>& v, uint32_t x)
{
  v.push_back(static_cast<uint8_t>(x >> 24));
  v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}

inline void put64(std::vector<uint8_t>& v, uint64_t x)
{
  for (int i = 7; i >= 0; i--) {
    v.push_back(static_cast<uint8_t>(x >> (i * 8)));
  }
}

inline void put_fourcc(std::vector<uint8_t>& v, const char* s)
{
  v.push_back(static_cast<uint8_t>(s[0]));
  v.push_back(static_cast<uint8_t>(s[1]));
  v.push_back(static_cast<uint8_t>(s[2]));
  v.push_back(static_cast<uint8_t>(s[3]));
}

inline void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src)
{
  dst.insert(dst.end(), src.begin(), src.end());
}

// Wrap a payload in a box: [uint32 size][fourcc type][payload].
inline std::vector<uint8_t> box(const char* type, const std::vector<uint8_t>& payload)
{
  std::vector<uint8_t> b;
  put32(b, static_cast<uint32_t>(8 + payload.size()));
  put_fourcc(b, type);
  append(b, payload);
  return b;
}

inline std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts)
{
  std::vector<uint8_t> out;
  for (const auto& p : parts) {
    append(out, p);
  }
  return out;
}

struct Stts_entry
{
  uint32_t sample_count;
  uint32_t sample_delta;
};

// One 'saiz'/'saio' pair attached to the track. Every sample carries the same
// aux-info payload (constant size, at most 255 bytes, because 'saiz' stores the
// default size in a uint8_t). The payloads are appended to 'mdat' behind the
// sample data.
struct SampleAuxInfo
{
  const char* type = "suid";     // aux_info_type: 'suid' = GIMI content ID, 'stai' = TAI timestamp
  std::vector<uint8_t> data;     // per-sample payload, identical for all samples
  bool offset_past_eof = false;  // let 'saio' point beyond the end of the file
  uint32_t sample_count = 0;     // samples the 'saiz' describes; 0 -> p.num_samples. Set lower
                                 // to make 'saiz' cover fewer samples than the track has.
};

// One entry of the 'stsc' table.
struct Stsc_entry
{
  uint32_t first_chunk;
  uint32_t samples_per_chunk;
  uint32_t sample_description_index;
};

struct SequenceFileParams
{
  bool     mvhd_v1 = true;
  uint64_t mvhd_duration = std::numeric_limits<uint64_t>::max(); // indefinite sentinel
  uint32_t timescale = 1000;

  uint64_t mdhd_duration = 1;

  bool     with_editlist = true;
  uint64_t elst_segment_duration = 1; // must equal mdhd_duration to match the repeat pattern

  std::vector<Stts_entry> stts = {{1, 1}};

  uint32_t num_samples = 1;
  uint32_t sample_size = 1;
  std::vector<uint8_t> sample_bytes; // if set, repeat these bytes for every sample

  // Override these two to build a track of a different kind, e.g. a 'pict' track with 'hvc1' samples.
  const char* handler_type = "meta";
  std::vector<uint8_t> sample_entry; // complete sample entry box, empty -> 'urim'
  uint32_t first_chunk_num_samples = 0; // nonzero -> split into two chunks
  std::vector<uint8_t> second_sample_entry;
  std::vector<uint8_t> second_sample_bytes; // same length as sample_bytes

  // Nonempty -> write this 'stsc' table and 'num_chunks' chunks, all pointing to the start of
  // the sample data. The samples per chunk must add up to num_samples.
  std::vector<Stsc_entry> stsc_entries;
  uint32_t num_chunks = 0;

  std::vector<SampleAuxInfo> aux_infos;
};

inline std::vector<uint8_t> build_sequence_file(const SequenceFileParams& p)
{
  const uint32_t sample_size = p.sample_bytes.empty() ? p.sample_size : static_cast<uint32_t>(p.sample_bytes.size());
  const bool two_chunks = p.first_chunk_num_samples != 0;
  // --- ftyp
  std::vector<uint8_t> ftyp_payload;
  put_fourcc(ftyp_payload, "msf1"); // major brand: HEIF image sequence
  put32(ftyp_payload, 0);           // minor version
  put_fourcc(ftyp_payload, "msf1");
  put_fourcc(ftyp_payload, "isom");
  std::vector<uint8_t> ftyp = box("ftyp", ftyp_payload);

  // --- mvhd
  std::vector<uint8_t> mvhd_payload;
  if (p.mvhd_v1) {
    put32(mvhd_payload, 0x01000000); // version 1, flags 0
    put64(mvhd_payload, 0);          // creation_time
    put64(mvhd_payload, 0);          // modification_time
    put32(mvhd_payload, p.timescale);
    put64(mvhd_payload, p.mvhd_duration);
  }
  else {
    put32(mvhd_payload, 0x00000000); // version 0, flags 0
    put32(mvhd_payload, 0);          // creation_time
    put32(mvhd_payload, 0);          // modification_time
    put32(mvhd_payload, p.timescale);
    put32(mvhd_payload, static_cast<uint32_t>(p.mvhd_duration));
  }
  put32(mvhd_payload, 0x00010000); // rate 1.0
  put16(mvhd_payload, 0x0100);     // volume
  put16(mvhd_payload, 0);          // reserved
  put32(mvhd_payload, 0);          // reserved
  put32(mvhd_payload, 0);          // reserved
  for (int i = 0; i < 9; i++) put32(mvhd_payload, 0); // matrix
  for (int i = 0; i < 6; i++) put32(mvhd_payload, 0); // pre_defined
  put32(mvhd_payload, 2);          // next_track_ID
  std::vector<uint8_t> mvhd = box("mvhd", mvhd_payload);

  // --- tkhd (version 1)
  std::vector<uint8_t> tkhd_payload;
  put32(tkhd_payload, 0x01000007); // version 1, flags = enabled|in_movie|in_preview
  put64(tkhd_payload, 0);          // creation_time
  put64(tkhd_payload, 0);          // modification_time
  put32(tkhd_payload, 1);          // track_ID
  put32(tkhd_payload, 0);          // reserved
  put64(tkhd_payload, 0);          // duration
  put64(tkhd_payload, 0);          // reserved
  put16(tkhd_payload, 0);          // layer
  put16(tkhd_payload, 0);          // alternate_group
  put16(tkhd_payload, 0);          // volume
  put16(tkhd_payload, 0);          // reserved
  for (int i = 0; i < 9; i++) put32(tkhd_payload, 0); // matrix
  put32(tkhd_payload, 0);          // width
  put32(tkhd_payload, 0);          // height
  std::vector<uint8_t> tkhd = box("tkhd", tkhd_payload);

  // --- edts / elst (version 1, flags = repeat)
  std::vector<uint8_t> edts;
  if (p.with_editlist) {
    std::vector<uint8_t> elst_payload;
    put32(elst_payload, 0x01000001); // version 1, flags = Repeat_EditList
    put32(elst_payload, 1);          // entry_count
    put64(elst_payload, p.elst_segment_duration);
    put64(elst_payload, 0);          // media_time
    put16(elst_payload, 1);          // media_rate_integer
    put16(elst_payload, 0);          // media_rate_fraction
    edts = box("edts", box("elst", elst_payload));
  }

  // --- mdhd (version 0)
  std::vector<uint8_t> mdhd_payload;
  put32(mdhd_payload, 0x00000000); // version 0, flags 0
  put32(mdhd_payload, 0);          // creation_time
  put32(mdhd_payload, 0);          // modification_time
  put32(mdhd_payload, p.timescale);
  put32(mdhd_payload, static_cast<uint32_t>(p.mdhd_duration));
  put16(mdhd_payload, 0x55c4);     // language ('und')
  put16(mdhd_payload, 0);          // pre_defined
  std::vector<uint8_t> mdhd = box("mdhd", mdhd_payload);

  // --- hdlr (handler type 'meta' by default)
  std::vector<uint8_t> hdlr_payload;
  put32(hdlr_payload, 0x00000000); // version 0, flags 0
  put32(hdlr_payload, 0);          // pre_defined
  put_fourcc(hdlr_payload, p.handler_type);
  put32(hdlr_payload, 0);          // reserved
  put32(hdlr_payload, 0);          // reserved
  put32(hdlr_payload, 0);          // reserved
  hdlr_payload.push_back(0);       // name (empty, null-terminated)
  std::vector<uint8_t> hdlr = box("hdlr", hdlr_payload);

  // --- nmhd (null media header for metadata tracks)
  std::vector<uint8_t> nmhd_payload;
  put32(nmhd_payload, 0x00000000); // version 0, flags 0
  std::vector<uint8_t> nmhd = box("nmhd", nmhd_payload);

  // --- stsd with one or two sample entries ('urim', URI meta, by default)
  std::vector<uint8_t> urim_payload;
  for (int i = 0; i < 6; i++) urim_payload.push_back(0); // SampleEntry reserved
  put16(urim_payload, 1);                                // data_reference_index
  std::vector<uint8_t> urim = box("urim", urim_payload);

  std::vector<uint8_t> stsd_payload;
  put32(stsd_payload, 0x00000000); // version 0, flags 0
  put32(stsd_payload, two_chunks ? 2 : 1); // entry_count
  append(stsd_payload, p.sample_entry.empty() ? urim : p.sample_entry);
  if (two_chunks) append(stsd_payload, p.second_sample_entry);
  std::vector<uint8_t> stsd = box("stsd", stsd_payload);

  // --- stts
  std::vector<uint8_t> stts_payload;
  put32(stts_payload, 0x00000000); // version 0, flags 0
  put32(stts_payload, static_cast<uint32_t>(p.stts.size()));
  for (const auto& e : p.stts) {
    put32(stts_payload, e.sample_count);
    put32(stts_payload, e.sample_delta);
  }
  std::vector<uint8_t> stts = box("stts", stts_payload);

  // --- stsc: one or two chunks, each with its own sample description, or the given table
  std::vector<uint8_t> stsc_payload;
  put32(stsc_payload, 0x00000000); // version 0, flags 0
  if (!p.stsc_entries.empty()) {
    put32(stsc_payload, static_cast<uint32_t>(p.stsc_entries.size())); // entry_count
    for (const auto& entry : p.stsc_entries) {
      put32(stsc_payload, entry.first_chunk);
      put32(stsc_payload, entry.samples_per_chunk);
      put32(stsc_payload, entry.sample_description_index);
    }
  }
  else {
    put32(stsc_payload, two_chunks ? 2 : 1); // entry_count
    put32(stsc_payload, 1);          // first_chunk
    put32(stsc_payload, two_chunks ? p.first_chunk_num_samples : p.num_samples); // samples_per_chunk
    put32(stsc_payload, 1);          // sample_description_index
    if (two_chunks) {
      put32(stsc_payload, 2); // first_chunk
      put32(stsc_payload, p.num_samples - p.first_chunk_num_samples);
      put32(stsc_payload, 2); // sample_description_index
    }
  }
  std::vector<uint8_t> stsc = box("stsc", stsc_payload);

  // --- stsz: fixed sample size
  std::vector<uint8_t> stsz_payload;
  put32(stsz_payload, 0x00000000);  // version 0, flags 0
  put32(stsz_payload, sample_size); // fixed sample size (non-zero -> no per-sample array)
  put32(stsz_payload, p.num_samples); // sample_count
  std::vector<uint8_t> stsz = box("stsz", stsz_payload);

  // --- saiz: one per aux info, constant per-sample size
  auto aux_sample_count = [&](const SampleAuxInfo& aux) {
    return aux.sample_count ? aux.sample_count : p.num_samples;
  };

  std::vector<std::vector<uint8_t>> saiz_boxes;
  for (const auto& aux : p.aux_infos) {
    std::vector<uint8_t> saiz_payload;
    put32(saiz_payload, 0x00000001); // version 0, flags = aux_info_type present
    put_fourcc(saiz_payload, aux.type);
    put32(saiz_payload, 0);          // aux_info_type_parameter
    saiz_payload.push_back(static_cast<uint8_t>(aux.data.size())); // default_sample_info_size
    put32(saiz_payload, aux_sample_count(aux)); // sample_count
    saiz_boxes.push_back(box("saiz", saiz_payload));
  }

  // --- stco / saio: offsets are patched below to point into the mdat payload
  auto make_stco = [](const std::vector<uint32_t>& offsets) {
    std::vector<uint8_t> stco_payload;
    put32(stco_payload, 0x00000000); // version 0, flags 0
    put32(stco_payload, static_cast<uint32_t>(offsets.size())); // entry_count
    for (uint32_t offset : offsets) put32(stco_payload, offset);
    return box("stco", stco_payload);
  };

  auto make_saio = [](const SampleAuxInfo& aux, uint32_t offset) {
    std::vector<uint8_t> saio_payload;
    put32(saio_payload, 0x00000001); // version 0, flags = aux_info_type present
    put_fourcc(saio_payload, aux.type);
    put32(saio_payload, 0);          // aux_info_type_parameter
    put32(saio_payload, 1);          // entry_count (single chunk)
    put32(saio_payload, offset);
    return box("saio", saio_payload);
  };

  auto assemble = [&](const std::vector<uint8_t>& stco,
                      const std::vector<std::vector<uint8_t>>& saio_boxes) {
    std::vector<uint8_t> stbl_payload = concat({stsd, stts, stsc, stsz, stco});
    for (const auto& b : saiz_boxes) append(stbl_payload, b);
    for (const auto& b : saio_boxes) append(stbl_payload, b);
    std::vector<uint8_t> stbl = box("stbl", stbl_payload);
    std::vector<uint8_t> minf = box("minf", concat({nmhd, stbl}));
    std::vector<uint8_t> mdia = box("mdia", concat({mdhd, hdlr, minf}));
    std::vector<uint8_t> trak = box("trak", concat({tkhd, edts, mdia}));
    std::vector<uint8_t> moov = box("moov", concat({mvhd, trak}));
    return moov;
  };

  // --- mdat payload: num_samples * sample_size bytes of arbitrary content,
  //     followed by one block of aux-info payloads per saiz/saio pair.
  std::vector<uint8_t> mdat_payload(static_cast<size_t>(p.num_samples) * sample_size);
  for (size_t i = 0; i < mdat_payload.size(); i++) {
    const auto& sample_bytes = (two_chunks && i >= static_cast<size_t>(p.first_chunk_num_samples) * sample_size)
                               ? p.second_sample_bytes : p.sample_bytes;
    mdat_payload[i] = sample_bytes.empty() ? static_cast<uint8_t>(0xA0 + (i & 0x0f))
                                          : sample_bytes[i % sample_size];
  }

  std::vector<size_t> aux_block_offsets; // relative to the start of the mdat payload
  for (const auto& aux : p.aux_infos) {
    aux_block_offsets.push_back(mdat_payload.size());
    for (uint32_t s = 0; s < aux_sample_count(aux); s++) {
      append(mdat_payload, aux.data);
    }
  }

  // Box sizes are independent of the offset values, so we can size the file with
  // placeholders, then patch the real offsets in one pass.
  std::vector<std::vector<uint8_t>> saio_placeholders;
  for (const auto& aux : p.aux_infos) {
    saio_placeholders.push_back(make_saio(aux, 0));
  }
  const size_t num_chunks = !p.stsc_entries.empty() ? p.num_chunks : (two_chunks ? 2 : 1);
  std::vector<uint8_t> moov0 = assemble(make_stco(std::vector<uint32_t>(num_chunks, 0)), saio_placeholders);
  uint32_t mdat_payload_offset = static_cast<uint32_t>(ftyp.size() + moov0.size() + 8 /* mdat header */);
  uint32_t file_size = static_cast<uint32_t>(mdat_payload_offset + mdat_payload.size());

  std::vector<std::vector<uint8_t>> saio_boxes;
  for (size_t i = 0; i < p.aux_infos.size(); i++) {
    uint32_t offset = p.aux_infos[i].offset_past_eof
                      ? file_size + 4096
                      : mdat_payload_offset + static_cast<uint32_t>(aux_block_offsets[i]);
    saio_boxes.push_back(make_saio(p.aux_infos[i], offset));
  }
  std::vector<uint32_t> chunk_offsets = {mdat_payload_offset};
  if (!p.stsc_entries.empty()) chunk_offsets.assign(num_chunks, mdat_payload_offset);
  else if (two_chunks) chunk_offsets.push_back(mdat_payload_offset + p.first_chunk_num_samples * sample_size);
  std::vector<uint8_t> moov = assemble(make_stco(chunk_offsets), saio_boxes);

  std::vector<uint8_t> mdat = box("mdat", mdat_payload);

  return concat({ftyp, moov, mdat});
}


// --- Rewriting the 'moov' box of an encoded sequence

inline uint32_t get32(const std::vector<uint8_t>& v, size_t offset)
{
  return (static_cast<uint32_t>(v[offset]) << 24) |
         (static_cast<uint32_t>(v[offset + 1]) << 16) |
         (static_cast<uint32_t>(v[offset + 2]) << 8) |
         static_cast<uint32_t>(v[offset + 3]);
}

// Replace the top-level 'moov' box by a 'free' box of the same size and append the 'moov' box returned by
// 'rewrite' (std::optional<std::vector<uint8_t>>(const std::vector<uint8_t>& moov)) at the end of the file.
// This keeps the chunk offsets into 'mdat' valid. Returns false if there is no well-formed top-level 'moov'
// box or if 'rewrite' returns no box.
template <typename Rewrite>
inline bool rewrite_moov_keeping_offsets(std::vector<uint8_t>& file, Rewrite rewrite)
{
  for (size_t offset = 0; file.size() - offset >= 8;) {
    const uint32_t size = get32(file, offset);
    if (size < 8 || size > file.size() - offset) {
      return false;
    }

    if (std::string(file.begin() + offset + 4, file.begin() + offset + 8) == "moov") {
      const std::vector<uint8_t> moov(file.begin() + offset, file.begin() + offset + size);
      std::optional<std::vector<uint8_t>> replacement = rewrite(moov);
      if (!replacement) {
        return false;
      }

      std::copy_n("free", 4, file.begin() + offset + 4);
      std::fill(file.begin() + offset + 8, file.begin() + offset + size, 0);
      append(file, *replacement);
      return true;
    }

    offset += size;
  }

  return false;
}

namespace detail {

inline bool split_first_sample_into_own_chunk(const std::vector<uint8_t>& data, bool different_descriptions,
                                              uint32_t& num_samples, uint32_t& first_sample_size,
                                              std::vector<uint8_t>& output)
{
  for (size_t offset = 0; offset < data.size();) {
    if (data.size() - offset < 8) {
      return false;
    }

    const uint32_t size = get32(data, offset);
    if (size < 8 || size > data.size() - offset) {
      return false;
    }

    const std::string type(data.begin() + offset + 4, data.begin() + offset + 8);
    std::vector<uint8_t> payload(data.begin() + offset + 8, data.begin() + offset + size);

    if (type == "moov" || type == "trak" || type == "mdia" || type == "minf" || type == "stbl") {
      // Read stsz before rewriting stco, regardless of their order in the file.
      if (type == "stbl") {
        for (size_t child = 0; payload.size() - child >= 8;) {
          const uint32_t child_size = get32(payload, child);
          if (child_size < 8 || child_size > payload.size() - child) {
            return false;
          }

          if (std::string(payload.begin() + child + 4, payload.begin() + child + 8) == "stsz") {
            if (child_size < 20) {
              return false;
            }

            num_samples = get32(payload, child + 16);
            first_sample_size = get32(payload, child + 12);
            if (first_sample_size == 0) {
              if (child_size < 24) {
                return false;
              }
              first_sample_size = get32(payload, child + 20);
            }
          }

          child += child_size;
        }
      }

      std::vector<uint8_t> rewritten;
      if (!split_first_sample_into_own_chunk(payload, different_descriptions, num_samples, first_sample_size,
                                             rewritten)) {
        return false;
      }
      payload = std::move(rewritten);
    }
    else if (type == "stsd" && different_descriptions) {
      if (payload.size() < 8 || get32(payload, 4) != 1) {
        return false;
      }

      const std::vector<uint8_t> entry(payload.begin() + 8, payload.end());
      payload[7] = 2;
      append(payload, entry);
    }
    else if (type == "stsc") {
      if (num_samples < 2) {
        return false;
      }

      payload.clear();
      for (uint32_t value : {0u, 2u,
                             1u, 1u, 1u,
                             2u, num_samples - 1, different_descriptions ? 2u : 1u}) {
        put32(payload, value);
      }
    }
    else if (type == "stco") {
      if (payload.size() < 12 || get32(payload, 4) != 1 || first_sample_size == 0) {
        return false;
      }

      const uint32_t first_offset = get32(payload, 8);
      payload.clear();
      for (uint32_t value : {0u, 2u, first_offset, first_offset + first_sample_size}) {
        put32(payload, value);
      }
    }

    append(output, box(type.c_str(), payload));
    offset += size;
  }

  return true;
}

} // namespace detail

// Rewrite the 'moov' box of a single-track sequence that stores all samples in one chunk ('stco' with 32-bit
// offsets), so that the first sample is in a chunk of its own and the remaining samples in a second chunk.
// With 'different_descriptions', 'stsd' gets a copy of its sample entry and the second chunk uses it.
inline std::optional<std::vector<uint8_t>> split_first_sample_into_own_chunk(const std::vector<uint8_t>& moov,
                                                                             bool different_descriptions)
{
  uint32_t num_samples = 0;
  uint32_t first_sample_size = 0;
  std::vector<uint8_t> output;
  if (!detail::split_first_sample_into_own_chunk(moov, different_descriptions, num_samples, first_sample_size,
                                                 output)) {
    return std::nullopt;
  }

  return output;
}

} // namespace seqfile

#endif
