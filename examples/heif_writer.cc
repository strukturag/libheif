/*
 * libheif example application "heif".
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of heif, an example application using libheif.
 *
 * heif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * heif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with heif.  If not, see <http://www.gnu.org/licenses/>.
 */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <errno.h>
#include <string.h>

#include "bitstream.h"
#include "box.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <getopt.h>

using namespace heif;


int main(int argc, char** argv)
{
  StreamWriter writer;

  Box_ftyp ftyp;
  ftyp.set_major_brand(fourcc("heic"));
  ftyp.set_minor_version(0);
  ftyp.add_compatible_brand(fourcc("mif1"));
  ftyp.add_compatible_brand(fourcc("heic"));
  ftyp.write(writer);


  Box_meta meta;

  auto hdlr = std::make_shared<Box_hdlr>();
  meta.append_child_box(hdlr);

  auto pitm = std::make_shared<Box_pitm>();
  pitm->set_item_ID(4711);
  meta.append_child_box(pitm);

  auto iloc = std::make_shared<Box_iloc>();
  iloc->append_data(4711, std::vector<uint8_t> { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 });
  iloc->append_data(4712, std::vector<uint8_t> { 1,2,3,4,5,6,7,8,9,10 });
  iloc->append_data(4712, std::vector<uint8_t> { 1,2,3,4,5 });
  meta.append_child_box(iloc);

  auto infe = std::make_shared<Box_infe>();
  infe->set_hidden_item(true);
  infe->set_item_ID(4712);
  infe->set_item_type("hvc1");
  infe->set_item_name("Nice image");

  auto iinf = std::make_shared<Box_iinf>();
  iinf->append_child_box(infe);
  meta.append_child_box(iinf);

  auto iprp = std::make_shared<Box_iprp>();
  auto ipco = std::make_shared<Box_ipco>();
  auto ipma = std::make_shared<Box_ipma>();
  iprp->append_child_box(ipco);
  iprp->append_child_box(ipma);

  ipma->add_property_for_item_ID(4711, Box_ipma::PropertyAssociation { true, 1 });
  ipma->add_property_for_item_ID(4711, Box_ipma::PropertyAssociation { false, 0 });
  ipma->add_property_for_item_ID(4712, Box_ipma::PropertyAssociation { false, 2 });

  auto hvcC = std::make_shared<Box_hvcC>();
  hvcC->append_nal_data( std::vector<uint8_t> { 10,9,8,7,6,5,4,3,2,1 } );
  ipco->append_child_box(hvcC);

  auto ispe = std::make_shared<Box_ispe>();
  ispe->set_size(1920,1080);
  ipco->append_child_box(ispe);
  meta.append_child_box(iprp);

  meta.derive_box_version_recursive();
  meta.write(writer);

  iloc->write_mdat_after_iloc(writer);

  std::ofstream ostr("out.heic");
  const auto& data = writer.get_data();
  ostr.write( (const char*)data.data(), data.size() );

  return 0;
}
