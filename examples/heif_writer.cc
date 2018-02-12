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
#include "heif_context.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <getopt.h>

using namespace heif;


void test1()
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
}




void test2(const char* h265_file)
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
  pitm->set_item_ID(1);
  meta.append_child_box(pitm);

  auto iloc = std::make_shared<Box_iloc>();
  //iloc->append_data(4712, std::vector<uint8_t> { 1,2,3,4,5 });
  meta.append_child_box(iloc);

  auto infe = std::make_shared<Box_infe>();
  infe->set_hidden_item(false);
  infe->set_item_ID(1);
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

  ipma->add_property_for_item_ID(1, Box_ipma::PropertyAssociation { true, 1 });

  auto hvcC = std::make_shared<Box_hvcC>();
  //hvcC->append_nal_data( std::vector<uint8_t> { 10,9,8,7,6,5,4,3,2,1 } );
  ipco->append_child_box(hvcC);

  auto ispe = std::make_shared<Box_ispe>();
  ispe->set_size(1920,1080);
  ipco->append_child_box(ispe);
  meta.append_child_box(iprp);


  std::ifstream istr(h265_file);
  int state=0;

  bool first=true;
  bool eof=false;
  std::streampos prev_start_code_start;
  std::streampos start_code_start;
  //uint8_t nal_type;

  for (;;) {
    bool dump_nal = false;

    int c = istr.get();

    if (state==3) {
      state=0;
    }

    //printf("read c=%02x\n",c);

    if (c==0 && state<=1) {
      state++;
    }
    else if (c==0) {
      // NOP
    }
    else if (c==1 && state==2) {
      start_code_start = istr.tellg() - (std::streampos)3;
      dump_nal = true;
      state=3;
    }
    else {
      state=0;
    }

    //printf("-> state= %d\n",state);

    if (istr.eof()) {
      printf("to end of file\n");
      istr.clear();
      istr.seekg(0, std::ios::end);
      start_code_start = istr.tellg();
      printf("end of file pos: %04x\n",(uint32_t)start_code_start);
      dump_nal = true;
      eof = true;
    }

    if (dump_nal) {
      if (first) {
        first = false;
      }
      else {
        std::vector<uint8_t> nal_data;
        size_t length = start_code_start - (prev_start_code_start+(std::streampos)3);

        printf("found start code at position: %08x (prev: %08x)\n",
               (uint32_t)start_code_start,
               (uint32_t)prev_start_code_start);

        nal_data.resize(length);

        istr.seekg(prev_start_code_start+(std::streampos)3);
        istr.read((char*)nal_data.data(), length);

        istr.seekg(start_code_start+(std::streampos)3);

        printf("read nal %02x with length %08x\n",nal_data[0], (uint32_t)length);

        int nal_type = (nal_data[0]>>1);

        switch (nal_type) {
        case 0x20:
        case 0x21:
        case 0x22:
          hvcC->append_nal_data(nal_data);
          break;

        default: {
          std::vector<uint8_t> nal_data_with_size;
          nal_data_with_size.resize(nal_data.size() + 4);

          memcpy(nal_data_with_size.data()+4, nal_data.data(), nal_data.size());
          nal_data_with_size[0] = ((nal_data.size()>>24) & 0xFF);
          nal_data_with_size[1] = ((nal_data.size()>>16) & 0xFF);
          nal_data_with_size[2] = ((nal_data.size()>> 8) & 0xFF);
          nal_data_with_size[3] = ((nal_data.size()>> 0) & 0xFF);

          iloc->append_data(1, nal_data_with_size);
        }
          break;
        }
      }

      prev_start_code_start = start_code_start;
    }

    if (eof) {
      break;
    }
  }


  meta.derive_box_version_recursive();
  meta.write(writer);

  iloc->write_mdat_after_iloc(writer);

  std::ofstream ostr("out.heic");
  const auto& data = writer.get_data();
  ostr.write( (const char*)data.data(), data.size() );
}


void test3(const char* h265_file)
{
  StreamWriter writer;

  HeifContext ctx;
  ctx.new_empty_heif();

  ctx.write(writer);

  std::ofstream ostr("out.heic");
  const auto& data = writer.get_data();
  ostr.write( (const char*)data.data(), data.size() );
};


int main(int argc, char** argv)
{
  //test1();
  //test2(argv[1]);
  test3(argv[1]);

  return 0;
}
