/*
 * HEIF codec.
 * Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "brands.h"
#include "file.h"


static bool check_mif1(const HeifContext* ctx)
{
  auto file = ctx->get_heif_file();

  auto meta = file->get_meta_box();
  if (!meta || meta->get_version() != 0) {
    return false;
  }

  auto hdlr = meta->get_child_box<Box_hdlr>();
  if (!hdlr || hdlr->get_version() != 0) {
    return false;
  }

  auto iloc = meta->get_child_box<Box_iloc>();
  if (!iloc || iloc->get_version() > 2) {
    return false;
  }

  auto iinf = meta->get_child_box<Box_iinf>();
  if (!iinf || iinf->get_version() > 1) {
    return false;
  }

  auto infe = iinf->get_child_box<Box_infe>();
  if (!infe || infe->get_version() < 2 || infe->get_version() > 3) {
    return false;
  }

  auto pitm = meta->get_child_box<Box_pitm>();
  if (!pitm || pitm->get_version() > 1) {
    return false;
  }

  auto iprp = meta->get_child_box<Box_iprp>();
  if (!iprp) {
    return false;
  }

  return true;
}


std::vector<heif_brand2> compute_compatible_brands(const HeifContext* ctx, heif_brand2* out_main_brand)
{
  std::vector<heif_brand2> compatible_brands;


  bool is_mif1 = check_mif1(ctx);

  if (is_mif1) {
    compatible_brands.push_back(heif_brand2_mif1);
  }

  return compatible_brands;
}

