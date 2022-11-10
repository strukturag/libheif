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

#include <mutex>

using namespace heif;

void heif_unload_all_plugins();

#if ENABLE_PLUGIN_LOADING

void heif_unregister_encoder_plugin(const heif_encoder_plugin* plugin);

std::vector<std::string> get_plugin_paths();

#endif


static int heif_library_initialization_count = 0;
static bool default_plugins_registered = true; // because they are implicitly registered at startup


static std::recursive_mutex& heif_init_mutex()
{
  static std::recursive_mutex init_mutex;
  return init_mutex;
}


struct heif_error heif_init(struct heif_init_params*)
{
  std::lock_guard<std::recursive_mutex> lock(heif_init_mutex());

  heif_library_initialization_count++;

  if (heif_library_initialization_count == 1) {

    // --- initialize builtin plugins

    if (!default_plugins_registered) {
      register_default_plugins();
    }

#if ENABLE_PLUGIN_LOADING
    struct heif_error err{};
    std::vector<std::string> plugin_paths = get_plugin_paths();

    if (plugin_paths.empty()) {
      // --- load plugins from default directory

      err = heif_load_plugins(LIBHEIF_PLUGIN_DIRECTORY, nullptr, nullptr, 0);
      if (err.code != 0) {
        return err;
      }
    }
    else {
      for (const auto& dir : plugin_paths) {
        err = heif_load_plugins(dir.c_str(), nullptr, nullptr, 0);
        if (err.code != 0) {
          return err;
        }
      }
    }
#endif
  }

  return {heif_error_Ok, heif_suberror_Unspecified, Error::kSuccess};
}


static void heif_unregister_decoder_plugins()
{
  for (const auto* plugin : heif::s_decoder_plugins) {
    if (plugin->deinit_plugin) {
      (*plugin->deinit_plugin)();
    }
  }
  heif::s_decoder_plugins.clear();
}

static void heif_unregister_encoder_plugins()
{
  for (const auto& plugin : heif::s_encoder_descriptors) {
    if (plugin->plugin->cleanup_plugin) {
      (*plugin->plugin->cleanup_plugin)();
    }
  }
  heif::s_encoder_descriptors.clear();
}

#if defined(__linux__) && ENABLE_PLUGIN_LOADING

// Currently only linux, as we don't have dynamic plugins for other systems yet.
void heif_unregister_encoder_plugin(const heif_encoder_plugin* plugin)
{
  if (plugin->cleanup_plugin) {
    (*plugin->cleanup_plugin)();
  }

  for (auto iter = heif::s_encoder_descriptors.begin() ; iter != heif::s_encoder_descriptors.end(); ++iter) {
    if ((*iter)->plugin == plugin) {
      heif::s_encoder_descriptors.erase(iter);
      return;
    }
  }
}

#endif

void heif_deinit()
{
  std::lock_guard<std::recursive_mutex> lock(heif_init_mutex());

  if (heif_library_initialization_count == 0) {
    // This case should never happen (heif_deinit() is called more often then heif_init()).
    return;
  }

  heif_library_initialization_count--;

  if (heif_library_initialization_count == 0) {
    heif_unregister_decoder_plugins();
    heif_unregister_encoder_plugins();
    default_plugins_registered = false;

    heif_unload_all_plugins();
  }
}


// This could be inside ENABLE_PLUGIN_LOADING, but the "include-what-you-use" checker cannot process this.
#include <vector>
#include <string>

#if ENABLE_PLUGIN_LOADING
struct loaded_plugin
{
  void* plugin_library_handle = nullptr;
  struct heif_plugin_info* info = nullptr;
  int openCnt = 0;
};

static std::vector<loaded_plugin> sLoadedPlugins;

__attribute__((unused)) static heif_error error_dlopen{heif_error_Plugin_loading_error, heif_suberror_Plugin_loading_error, "Cannot open plugin (dlopen)."};
__attribute__((unused)) static heif_error error_plugin_not_loaded{heif_error_Plugin_loading_error, heif_suberror_Plugin_is_not_loaded, "Trying to remove a plugin that is not loaded."};
__attribute__((unused)) static heif_error error_cannot_read_plugin_directory{heif_error_Plugin_loading_error, heif_suberror_Cannot_read_plugin_directory, "Cannot read plugin directory."};
#endif


#if ENABLE_PLUGIN_LOADING

__attribute__((unused)) static void unregister_plugin(const heif_plugin_info* info)
{
  switch (info->type) {
    case heif_plugin_type_encoder: {
      auto* encoder_plugin = static_cast<const heif_encoder_plugin*>(info->plugin);
      heif_unregister_encoder_plugin(encoder_plugin);
      break;
    }
    case heif_plugin_type_decoder: {
      // TODO
    }
  }
}

#endif


#if ENABLE_PLUGIN_LOADING && defined(__linux__)

#include <dlfcn.h>
#include <dirent.h>
#include <cstring>

struct heif_error heif_load_plugin(const char* filename, struct heif_plugin_info const** out_plugin)
{
  std::lock_guard<std::recursive_mutex> lock(heif_init_mutex());

  void* plugin_handle = dlopen(filename, RTLD_LAZY);
  if (!plugin_handle) {
    fprintf(stderr, "dlopen: %s\n", dlerror());
    return error_dlopen;
  }

  auto* plugin_info = (heif_plugin_info*) dlsym(plugin_handle, "plugin_info");
  if (!plugin_info) {
    fprintf(stderr, "dlsym: %s\n", dlerror());
    return error_dlopen;
  }

  // --- check whether the plugin is already loaded
  // If yes, return pointer to existing plugin.

  for (auto& p : sLoadedPlugins) {
    if (p.plugin_library_handle == plugin_handle) {
      if (out_plugin) {
        *out_plugin = p.info;
        p.openCnt++;
        return heif_error_ok;
      }
    }
  }

  loaded_plugin loadedPlugin;
  loadedPlugin.plugin_library_handle = plugin_handle;
  loadedPlugin.openCnt = 1;
  loadedPlugin.info = plugin_info;
  sLoadedPlugins.push_back(loadedPlugin);

  *out_plugin = plugin_info;

  switch (plugin_info->type) {
    case heif_plugin_type_encoder: {
      auto* encoder_plugin = static_cast<const heif_encoder_plugin*>(plugin_info->plugin);
      struct heif_error err = heif_register_encoder_plugin(encoder_plugin);
      if (err.code) {
        return err;
      }
      break;
    }

    case heif_plugin_type_decoder: {
      auto* decoder_plugin = static_cast<const heif_decoder_plugin*>(plugin_info->plugin);
      struct heif_error err = heif_register_decoder_plugin(decoder_plugin);
      if (err.code) {
        return err;
      }
      break;
    }
  }

  return heif_error_ok;
}


struct heif_error heif_unload_plugin(const struct heif_plugin_info* plugin)
{
  std::lock_guard<std::recursive_mutex> lock(heif_init_mutex());

  for (size_t i = 0; i < sLoadedPlugins.size(); i++) {
    auto& p = sLoadedPlugins[i];

    if (p.info == plugin) {
      dlclose(p.plugin_library_handle);
      p.openCnt--;

      if (p.openCnt == 0) {
        unregister_plugin(plugin);

        sLoadedPlugins[i] = sLoadedPlugins.back();
        sLoadedPlugins.pop_back();
      }

      return heif_error_ok;
    }
  }

  return error_plugin_not_loaded;
}


void heif_unload_all_plugins()
{
  std::lock_guard<std::recursive_mutex> lock(heif_init_mutex());

  for (auto& p : sLoadedPlugins) {
    unregister_plugin(p.info);

    for (int i = 0; i < p.openCnt; i++) {
      dlclose(p.plugin_library_handle);
    }
  }

  sLoadedPlugins.clear();
}


struct heif_error heif_load_plugins(const char* directory,
                                    const struct heif_plugin_info** out_plugins,
                                    int* out_nPluginsLoaded,
                                    int output_array_size)
{
  DIR* dir = opendir(directory);
  if (dir == nullptr) {
    return error_cannot_read_plugin_directory;
  }

  int nPlugins = 0;

  struct dirent* d;
  for (;;) {
    d = readdir(dir);
    if (d == nullptr) {
      break;
    }

    if ((d->d_type == DT_REG || d->d_type == DT_LNK) && strlen(d->d_name) > 3 &&
        strcmp(d->d_name + strlen(d->d_name) - 3, ".so") == 0) {
      std::string filename = directory;
      filename += '/';
      filename += d->d_name;
      //printf("load %s\n", filename.c_str());

      const struct heif_plugin_info* info = nullptr;
      auto err = heif_load_plugin(filename.c_str(), &info);
      if (err.code == 0) {
        if (out_plugins) {
          if (nPlugins == output_array_size) {
            break;
          }

          out_plugins[nPlugins] = info;
        }

        nPlugins++;
      }
    }
  }

  if (nPlugins < output_array_size && out_plugins) {
    out_plugins[nPlugins] = nullptr;
  }

  if (out_nPluginsLoaded) {
    *out_nPluginsLoaded = nPlugins;
  }

  closedir(dir);

  return heif_error_ok;
}


std::vector<std::string> get_plugin_paths()
{
  char* path_variable = getenv("LIBHEIF_PLUGIN_PATH");
  if (path_variable == nullptr) {
    return {};
  }

  // --- split LIBHEIF_PLUGIN_PATH value at ':' into separate directories

  std::vector<std::string> plugin_paths;

  std::istringstream paths(path_variable);
  std::string dir;
  while (getline(paths, dir, ':')) {
    plugin_paths.push_back(dir);
  }

  return plugin_paths;
}

#else
static heif_error heif_error_plugins_unsupported{heif_error_Unsupported_feature, heif_suberror_Unspecified, "Plugins are not supported"};

struct heif_error heif_load_plugin(const char* filename, struct heif_plugin_info const** out_plugin)
{
  return heif_error_plugins_unsupported;
}


struct heif_error heif_unload_plugin(const struct heif_plugin_info* plugin)
{
  return heif_error_plugins_unsupported;
}


void heif_unload_all_plugins() {}

struct heif_error heif_load_plugins(const char* directory,
                                    const struct heif_plugin_info** out_plugins,
                                    int* out_nPluginsLoaded,
                                    int output_array_size)
{
  return heif_error_plugins_unsupported;
}

std::vector<std::string> get_plugin_paths()
{
  return {};
}

#endif
