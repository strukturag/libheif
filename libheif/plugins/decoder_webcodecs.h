#ifndef THIRD_PARTY_LIBHEIF_LIBHEIF_PLUGINS_DECODER_WEBCODECS_H_
#define THIRD_PARTY_LIBHEIF_LIBHEIF_PLUGINS_DECODER_WEBCODECS_H_

#include "common_utils.h"

const struct heif_decoder_plugin* get_decoder_plugin_webcodecs();

#if PLUGIN_WEBCODECS
extern "C" {
MAYBE_UNUSED LIBHEIF_API extern heif_plugin_info plugin_info;
}
#endif

#endif  // THIRD_PARTY_LIBHEIF_LIBHEIF_PLUGINS_DECODER_WEBCODECS_H_
