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


#ifndef LIBHEIF_BENCHMARK_H
#define LIBHEIF_BENCHMARK_H

#include <memory>
#include <string>

struct heif_image;

double compute_psnr(heif_image* original_image, const std::string& encoded_file);

#endif //LIBHEIF_BENCHMARK_H
