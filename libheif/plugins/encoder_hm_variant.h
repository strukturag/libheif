/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBHEIF_ENCODER_HM_VARIANT_H
#define LIBHEIF_ENCODER_HM_VARIANT_H

#include "libheif/heif.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/* The HM encoder plugin can contain several versions of the HEVC reference software HM,
   the variants. Each variant lives in a C++ namespace of its own (see hm_as_library.h), and
   encoder_hm_variant.cc is compiled once for each of them.

   This is the interface between the plugin (encoder_hm.cc) and a variant. It does not
   depend on HM: the plugin does not see any declaration of HM.
 */

struct hm_variant_result
{
  enum class Status
  {
    ok,
    configuration_refused, // HM does not accept the options
    encoding_failed,
    out_of_memory
  };

  Status status = Status::ok;

  // The end of the text that HM has printed. When HM gives up, its last lines say why.
  std::string messages;
};


struct hm_variant
{
  // Version of HM, like "18.0" or "16.21_SCM8.8".
  const char* version;

  // Whether this version of HM has the screen content coding tools.
  bool has_screen_content_coding;

  // Encodes one picture.
  //
  // 'options' are options of the HM command line, like "--QP=20". The picture has to be as
  // large as SourceWidth and SourceHeight say, or smaller, in which case it is extended by
  // copies of its border.
  //
  // HM keeps part of its state in global variables: only one picture can be encoded at a
  // time by the same variant.
  hm_variant_result (* encode)(const std::vector<std::string>& options,
                               const heif_image* image,
                               std::deque<std::vector<uint8_t>>& out_nal_units);
};


// These functions only exist for the variants that are compiled in
// (HAVE_HM_VARIANT_LATEST, HAVE_HM_VARIANT_SCC).

const hm_variant* get_hm_variant_latest();

const hm_variant* get_hm_variant_scc();

#endif
