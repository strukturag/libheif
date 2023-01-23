/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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


#include "plugins_unix.h"
#include <sstream>

std::vector<std::string> get_plugin_directories_from_environment_variable_unix()
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
