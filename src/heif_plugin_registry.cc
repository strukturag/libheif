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

#include <string.h>

#include "heif_plugin_registry.h"

using namespace heif;


std::set<const struct heif_decoder_plugin*> heif::s_decoder_plugins;




struct encoder_descriptor_priority_order
{
  bool operator() (const std::unique_ptr<struct heif_encoder_descriptor>& a,
                   const std::unique_ptr<struct heif_encoder_descriptor>& b) const {
    return a->plugin->priority > b->plugin->priority;  // highest priority first
  }
};


std::set<std::unique_ptr<struct heif_encoder_descriptor>,
         encoder_descriptor_priority_order> s_encoder_descriptors;


void heif::register_decoder(const heif_decoder_plugin* decoder_plugin)
{
  if (decoder_plugin->init_plugin) {
    (*decoder_plugin->init_plugin)();
  }

  s_decoder_plugins.insert(decoder_plugin);
}


void heif::register_encoder(const heif_encoder_plugin* encoder_plugin)
{
  if (encoder_plugin->init_plugin) {
    (*encoder_plugin->init_plugin)();
  }

  auto descriptor = std::unique_ptr<struct heif_encoder_descriptor>(new heif_encoder_descriptor);
  descriptor->plugin = encoder_plugin;

  s_encoder_descriptors.insert(std::move(descriptor));
}


const struct heif_encoder_plugin* heif::get_encoder(enum heif_compression_format type)
{
  auto filtered_encoder_descriptors = get_filtered_encoder_descriptors(type, nullptr);
  if (filtered_encoder_descriptors.size()>0) {
    return filtered_encoder_descriptors[0]->plugin;
  }
  else {
    return nullptr;
  }
}


std::vector<const struct heif_encoder_descriptor*>
heif::get_filtered_encoder_descriptors(enum heif_compression_format format,
                                       const char* name)
{
  std::vector<const struct heif_encoder_descriptor*> filtered_descriptors;

  for (const auto& descr : s_encoder_descriptors) {
    const struct heif_encoder_plugin* plugin = descr->plugin;

    if (plugin->compression_format == format || format==heif_compression_undefined) {
      if (name == nullptr || strcmp(name, plugin->id_name)==0) {
        filtered_descriptors.push_back(descr.get());
      }
    }
  }


  // Note: since our std::set<> is ordered by priority, we do not have to sort our output

  return filtered_descriptors;
}
