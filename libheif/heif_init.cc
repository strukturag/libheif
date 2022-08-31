/*
 * HEIF codec.
 * Copyright (c) 2022 Dirk Farin <dirk.farin@gmail.com>
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

#include "heif_init.h"
#include "heif.h"
#include "error.h"
#include "heif_plugin_registry.h"

using namespace heif;


static int heif_library_initialization_count = 0;
static bool default_plugins_registered = true; // because they are implicitly registered at startup


struct heif_error heif_init(struct heif_init_params*)
{
  if (heif_library_initialization_count == 0 && !default_plugins_registered) {
    register_default_plugins();
  }

  heif_library_initialization_count++;

  return {heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess};
}


static void heif_unregister_decoder_plugins()
{
  for( const auto* plugin : heif::s_decoder_plugins ) {
    if( plugin->deinit_plugin ) {
      (*plugin->deinit_plugin)();
    }
  }
  heif::s_decoder_plugins.clear();
}

static void heif_unregister_encoder_plugins()
{
  for( const auto& plugin : heif::s_encoder_descriptors ) {
    if( plugin->plugin->cleanup_plugin ) {
      (*plugin->plugin->cleanup_plugin)();
    }
  }
  heif::s_encoder_descriptors.clear();
}

void heif_deinit()
{
  if (heif_library_initialization_count==0) {
    return;
  }

  heif_library_initialization_count--;
  if (heif_library_initialization_count == 0) {
    heif_unregister_decoder_plugins();
    heif_unregister_encoder_plugins();
    default_plugins_registered = false;
  }
}

