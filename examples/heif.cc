
#include "box.h"
#include "libde265/de265.h"

#include <fstream>
#include <iostream>

using namespace heif;


int main(int argc, char** argv)
{
  using heif::BoxHeader;
  using heif::Box;
  using heif::fourcc;

  std::ifstream istr(argv[1]);

  uint64_t maxSize = std::numeric_limits<uint64_t>::max();
  heif::BitstreamRange range(&istr, maxSize);

  std::shared_ptr<Box> meta_box;

  for (;;) {
    auto box = Box::read(range);

    if (!box || range.error()) {
      break;
    }

    heif::Indent indent;
    std::cout << "\n";
    std::cout << box->dump(indent);

    if (box->get_short_type() == fourcc("meta")) {
      meta_box = box;
    }
  }


  std::shared_ptr<Box_iloc> iloc_box = std::dynamic_pointer_cast<Box_iloc>(meta_box->get_child_box(fourcc("iloc")));
  std::shared_ptr<Box> iprp_box = meta_box->get_child_box(fourcc("iprp"));
  std::shared_ptr<Box> ipco_box = iprp_box->get_child_box(fourcc("ipco"));
  std::shared_ptr<Box_hvcC> hvcC_box = std::dynamic_pointer_cast<Box_hvcC>(ipco_box->get_child_box(fourcc("hvcC")));

  std::ifstream istr2(argv[1]);
  std::vector<uint8_t> hdrs = hvcC_box->get_headers();
  std::vector<uint8_t> data = iloc_box->read_all_data(istr2);

  de265_decoder_context* ctx = de265_new_decoder();
  de265_start_worker_threads(ctx,1);
  de265_push_data(ctx, hdrs.data(), hdrs.size(), 0, nullptr);
  de265_push_data(ctx, data.data(), data.size(), 0, nullptr);
  de265_push_end_of_stream(ctx);

  FILE* fh = fopen("out.bin", "wb");
  fwrite(hdrs.data(),1,hdrs.size(),fh);
  fwrite(data.data(),1,data.size(),fh);
  fclose(fh);

  for (;;) {
    int action = de265_get_action(ctx, 1);
    printf("libde265 action: %d\n",action);

    if (action==de265_action_get_image) {
      printf("image decoded !\n");
    }

    break;
    //#define de265_action_push_more_input     1
    //#define de265_action_end_of_stream       4
  }
}
