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
#include "heif_image.h"
#include "heif_api_structs.h"
#include "heif_encoder_x265.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <getopt.h>

extern "C" {
#include "x265.h"
#include <jpeglib.h>
}


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


std::shared_ptr<HeifPixelImage> loadJPEG(const char* filename)
{
  auto image = std::make_shared<HeifPixelImage>();


  // ### Code copied from LibVideoGfx and slightly modified to use HeifPixelImage

  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // open input file

  FILE * infile;
  if ((infile = fopen(filename, "rb")) == NULL) {
    fprintf(stderr, "can't open %s\n", filename);
    exit(1);
  }


  // initialize decompressor

  jpeg_create_decompress(&cinfo);

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_stdio_src(&cinfo, infile);

  jpeg_read_header(&cinfo, TRUE);

  if (cinfo.jpeg_color_space == JCS_GRAYSCALE)
    {
      cinfo.out_color_space = JCS_GRAYSCALE;

      jpeg_start_decompress(&cinfo);

      JSAMPARRAY buffer;
      buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


      // create destination image

      image->create(cinfo.output_width, cinfo.output_height,
                    heif_colorspace_YCbCr,
                    heif_chroma_monochrome);
      image->add_plane(heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);

      int y_stride;
      uint8_t* py = image->get_plane(heif_channel_Y, &y_stride);


      // read the image

      while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        memcpy(py + (cinfo.output_scanline-1)*y_stride, buffer, cinfo.output_width);
      }

    }
  else
    {
      cinfo.out_color_space = JCS_YCbCr;

      jpeg_start_decompress(&cinfo);

      JSAMPARRAY buffer;
      buffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * cinfo.output_components, 1);


      // create destination image

      image->create(cinfo.output_width, cinfo.output_height,
                    heif_colorspace_YCbCr,
                    heif_chroma_420);
      image->add_plane(heif_channel_Y, cinfo.output_width, cinfo.output_height, 8);
      image->add_plane(heif_channel_Cb, (cinfo.output_width+1)/2, (cinfo.output_height+1)/2, 8);
      image->add_plane(heif_channel_Cr, (cinfo.output_width+1)/2, (cinfo.output_height+1)/2, 8);

      int y_stride;
      int cb_stride;
      int cr_stride;
      uint8_t* py  = image->get_plane(heif_channel_Y, &y_stride);
      uint8_t* pcb = image->get_plane(heif_channel_Cb, &cb_stride);
      uint8_t* pcr = image->get_plane(heif_channel_Cr, &cr_stride);

      // read the image

      printf("jpeg size: %d %d\n",cinfo.output_width, cinfo.output_height);

      while (cinfo.output_scanline < cinfo.output_height) {
        JOCTET* bufp;

        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        bufp = buffer[0];

        int y = cinfo.output_scanline-1;

        for (unsigned int x=0;x<cinfo.output_width;x+=2) {
          py[y*y_stride + x] = *bufp++;
          pcb[y/2*cb_stride + x/2] = *bufp++;
          pcr[y/2*cr_stride + x/2] = *bufp++;

          if (x+1 < cinfo.output_width) {
            py[y*y_stride + x+1] = *bufp++;
          }

          bufp+=2;
        }


        if (cinfo.output_scanline < cinfo.output_height) {
          (void) jpeg_read_scanlines(&cinfo, buffer, 1);

          bufp = buffer[0];

          y = cinfo.output_scanline-1;

          for (unsigned int x=0;x<cinfo.output_width;x++) {
            py[y*y_stride + x] = *bufp++;
            bufp+=2;
          }
        }
      }
    }


  // cleanup

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  fclose(infile);

  return image;
}


void test3(const char* h265_file)
{
  // read h265 file

  std::ifstream istr(h265_file);
  istr.seekg(0, std::ios::end);
  size_t len = istr.tellg();
  istr.seekg(0, std::ios::beg);

  std::vector<uint8_t> h265data;
  h265data.resize(len);
  istr.read((char*)h265data.data(), len);


  // build HEIF file

  HeifContext ctx;
  ctx.new_empty_heif();

  auto image = ctx.add_new_hvc1_image();
  image->set_preencoded_hevc_image(h265data);
  ctx.set_primary_image(image);


  // write output

  StreamWriter writer;
  ctx.write(writer);

  std::ofstream ostr("out.heic");
  const auto& data = writer.get_data();
  ostr.write( (const char*)data.data(), data.size() );
};

void test4(const std::shared_ptr<HeifPixelImage>& img)
{
  int w = img->get_width() & ~1;
  int h = img->get_height() & ~1;

  printf("image size: %d %d\n",w,h);

  x265_param* param = x265_param_alloc();
  x265_param_default_preset(param, "slow", "ssim");
  x265_param_apply_profile(param, "mainstillpicture");

  param->sourceWidth = w;
  param->sourceHeight = h;
  param->fpsNum = 1;
  param->fpsDenom = 1;

  x265_encoder* enc = x265_encoder_open(param);

  x265_picture* pic = x265_picture_alloc();
  x265_picture_init(param, pic);

  pic->planes[0] = img->get_plane(heif_channel_Y, &pic->stride[0]);
  pic->planes[1] = img->get_plane(heif_channel_Cb, &pic->stride[1]);
  pic->planes[2] = img->get_plane(heif_channel_Cr, &pic->stride[2]);

  pic->bitDepth = 8;

  // int x265_encoder_headers(x265_encoder *, x265_nal **pp_nal, uint32_t *pi_nal);

  x265_nal* nals;
  uint32_t num_nals;
  bool first = true;

  for (;;) {
    int result = x265_encoder_encode(enc, &nals, &num_nals, first ? pic : NULL, NULL);

    printf("received %d NALs -> %d\n", num_nals, result);

    for (uint32_t i=0;i<num_nals;i++) {
      printf("%x (size=%d) %x %x %x %x %x\n",nals[i].type, nals[i].sizeBytes,
             nals[i].payload[0],
             nals[i].payload[1],
             nals[i].payload[2],
             nals[i].payload[3],
             nals[i].payload[4]);

      // std::cerr.write((const char*)nals[i].payload, nals[i].sizeBytes);
    }

    if (!first && result <= 0) {
      break;
    }

    first=false;
  }

  x265_picture_free(pic);
  x265_param_free(param);
}

void test5(std::shared_ptr<HeifPixelImage> image)
{
  heif_image img;
  img.image = image;

  const struct heif_encoder_plugin* encoder_plugin = get_encoder_plugin_x265();
  void* encoder;
  encoder_plugin->new_encoder(&encoder);

  encoder_plugin->encode_image(encoder, &img);

  for (;;) {
    uint8_t* data;
    int size;

    printf("get data\n");

    encoder_plugin->get_compressed_data(encoder, &data, &size, NULL);

    if (data==NULL) {
      break;
    }

    printf("size=%d: %x %x %x %x %x\n", size,
           data[0], data[1], data[2], data[3], data[4]);
  }
}

void test6(std::shared_ptr<HeifPixelImage> pixel_image)
{
#if 0
  // build HEIF file

  HeifContext ctx;
  ctx.new_empty_heif();

  auto image = ctx.add_new_hvc1_image();
  //image->set_preencoded_hevc_image(h265data);
  image->encode_image_as_hevc(pixel_image);
  ctx.set_primary_image(image);


  // write output

  StreamWriter writer;
  ctx.write(writer);

  std::ofstream ostr("out.heic");
  const auto& data = writer.get_data();
  ostr.write( (const char*)data.data(), data.size() );
#endif
}


void test_c_api(std::shared_ptr<HeifPixelImage> pixel_image)
{
  heif_image pixel_image_c_wrapper;
  pixel_image_c_wrapper.image = pixel_image;


  heif_context* context = heif_context_alloc();

  heif_context_new_heic(context);

#define MAX_ENCODERS 5
  heif_encoder* encoders[MAX_ENCODERS];
  int count = heif_context_get_encoders(context, heif_compression_HEVC, nullptr,
                                        encoders, MAX_ENCODERS);

  if (count>0) {
    printf("used encoder: %s\n", heif_encoder_get_name(encoders[0]));

    heif_encoder_init(encoders[0]);

    heif_encode_set_lossy_quality(encoders[0], 44);
    heif_encode_set_lossless(encoders[0], 0);

    struct heif_image_handle* handle;
    heif_error error = heif_context_encode_image(context,
                                                 &handle,
                                                 &pixel_image_c_wrapper,
                                                 encoders[0]);

    heif_image_handle_release(handle);

    heif_encoder_deinit(encoders[0]);

    error = heif_context_write_to_file(context, "out.heic");
  }
  else {
    fprintf(stderr,"no HEVC encoder available.\n");
  }

  heif_context_free(context);
}


int main(int argc, char** argv)
{
  //test1();
  //test2(argv[1]);
  //test3(argv[1]);

  std::shared_ptr<HeifPixelImage> image = loadJPEG(argv[1]);

  //test6(image);

  test_c_api(image);

  return 0;
}
