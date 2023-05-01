/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
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

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <utility>
#include <cstring>
#include <algorithm>

#include "plugin_registry.h"

#if HAVE_LIBDE265
#include "libheif/plugins/decoder_libde265.h"
#endif

#if HAVE_X265
#include "libheif/plugins/encoder_x265.h"
#endif

#if HAVE_AOM_ENCODER
#include "libheif/plugins/encoder_aom.h"
#endif

#if HAVE_AOM_DECODER
#include "libheif/plugins/decoder_aom.h"
#endif

#if HAVE_RAV1E
#include "libheif/plugins/encoder_rav1e.h"
#endif

#if HAVE_DAV1D
#include "libheif/plugins/decoder_dav1d.h"
#endif

#if HAVE_SvtEnc
#include "libheif/plugins/encoder_svt.h"
#endif

#if WITH_UNCOMPRESSED_CODEC
#include "libheif/plugins/encoder_uncompressed.h"
#endif


std::set<const struct heif_decoder_plugin*> s_decoder_plugins;

std::multiset<std::unique_ptr<struct heif_encoder_descriptor>,
              encoder_descriptor_priority_order> s_encoder_descriptors;

// Note: we cannot move this to 'heif_init' because we have to make sure that this is initialized
// AFTER the two global std::set above.
static class Register_Default_Plugins
{
public:
  Register_Default_Plugins()
  {
    register_default_plugins();
  }
} dummy;


void register_default_plugins()
{
#if HAVE_LIBDE265
  register_decoder(get_decoder_plugin_libde265());
#endif

#if HAVE_X265
  register_encoder(get_encoder_plugin_x265());
#endif

#if HAVE_AOM_ENCODER
  register_encoder(get_encoder_plugin_aom());
#endif

#if HAVE_AOM_DECODER
  register_decoder(get_decoder_plugin_aom());
#endif

#if HAVE_RAV1E
  register_encoder(get_encoder_plugin_rav1e());
#endif

#if HAVE_DAV1D
  register_decoder(get_decoder_plugin_dav1d());
#endif

#if HAVE_SvtEnc
  register_encoder(get_encoder_plugin_svt());
#endif

#if WITH_UNCOMPRESSED_CODEC
  register_encoder(get_encoder_plugin_uncompressed());
#endif
}


void register_decoder(const heif_decoder_plugin* decoder_plugin)
{
  if (decoder_plugin->init_plugin) {
    (*decoder_plugin->init_plugin)();
  }

  s_decoder_plugins.insert(decoder_plugin);
}


const struct heif_decoder_plugin* get_decoder(enum heif_compression_format type, const char* name_id)
{
  int highest_priority = 0;
  const struct heif_decoder_plugin* best_plugin = nullptr;

  for (const auto* plugin : s_decoder_plugins) {

    int priority = plugin->does_support_format(type);

    if (priority > 0 && name_id && plugin->plugin_api_version >= 3) {
      if (strcmp(name_id, plugin->id_name) == 0) {
        return plugin;
      }
    }

    if (priority > highest_priority) {
      highest_priority = priority;
      best_plugin = plugin;
    }
  }

  return best_plugin;
}


void register_encoder(const heif_encoder_plugin* encoder_plugin)
{
  if (encoder_plugin->init_plugin) {
    (*encoder_plugin->init_plugin)();
  }

  auto descriptor = std::unique_ptr<struct heif_encoder_descriptor>(new heif_encoder_descriptor);
  descriptor->plugin = encoder_plugin;

  s_encoder_descriptors.insert(std::move(descriptor));
}


const struct heif_encoder_plugin* get_encoder(enum heif_compression_format type)
{
  auto filtered_encoder_descriptors = get_filtered_encoder_descriptors(type, nullptr);
  if (filtered_encoder_descriptors.size() > 0) {
    return filtered_encoder_descriptors[0]->plugin;
  }
  else {
    return nullptr;
  }
}


std::vector<const struct heif_encoder_descriptor*>
get_filtered_encoder_descriptors(enum heif_compression_format format,
                                 const char* name)
{
  std::vector<const struct heif_encoder_descriptor*> filtered_descriptors;

  for (const auto& descr : s_encoder_descriptors) {
    const struct heif_encoder_plugin* plugin = descr->plugin;

    if (plugin->compression_format == format || format == heif_compression_undefined) {
      if (name == nullptr || strcmp(name, plugin->id_name) == 0) {
        filtered_descriptors.push_back(descr.get());
      }
    }
  }


  // Note: since our std::set<> is ordered by priority, we do not have to sort our output

  return filtered_descriptors;
}
