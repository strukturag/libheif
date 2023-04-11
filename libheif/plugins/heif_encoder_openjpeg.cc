
#include "libheif/heif.h"
#include "libheif/heif_plugin.h"
#include "heif_encoder_openjpeg.h"

#include <openjpeg.h>
#include <string.h>

#include <vector>
#include <string>
using namespace std;


static const int OPJ_PLUGIN_PRIORITY = 80;
static struct heif_error error_Ok = {heif_error_Ok, heif_suberror_Unspecified, "Success"};

struct encoder_struct_opj {
  // x265_encoder* encoder = nullptr;

  // x265_nal* nals = nullptr;
  // uint32_t num_nals = 0;
  // uint32_t nal_output_counter = 0;
  // int bit_depth = 0;

  heif_chroma chroma;

  // --- output

  std::vector<uint8_t> compressedData;
  bool data_read = false;

  // --- parameters

  // std::vector<parameter> parameters;

  // void add_param(const parameter&);

  // void add_param(const std::string& name, int value);

  // void add_param(const std::string& name, bool value);

  // void add_param(const std::string& name, const std::string& value);

  // parameter get_param(const std::string& name) const;

  // std::string preset;
  // std::string tune;

  // int logLevel = X265_LOG_NONE;
};

const char* opj_plugin_name() {
  // Human-readable name of the plugin
    return "OpenJPEG JPEG2000 Encoder";
}

void opj_init_plugin() {
  // Global plugin initialization (may be NULL)

}

void opj_cleanup_plugin() {
    // Global plugin cleanup (may be NULL).
    // Free data that was allocated in init_plugin()
}

struct heif_error opj_new_encoder(void** encoder_out) {

    struct encoder_struct_opj* encoder = new encoder_struct_opj();
    encoder->chroma = heif_chroma_interleaved_RGB; //default chroma

    *encoder_out = encoder;
    return error_Ok;
}

void opj_free_encoder(void* encoder_raw) {
    // Free the decoder context (heif_image can still be used after destruction)
    struct encoder_struct_opj* encoder = (struct encoder_struct_opj*) encoder_raw;
    delete encoder;
}

struct heif_error opj_set_parameter_quality(void* encoder, int quality) {
  return error_Ok;
}

struct heif_error opj_get_parameter_quality(void* encoder, int* quality) {
  return error_Ok;
}

struct heif_error opj_set_parameter_lossless(void* encoder, int lossless) {
  return error_Ok;
}

struct heif_error opj_get_parameter_lossless(void* encoder, int* lossless) {
  return error_Ok;
}

struct heif_error opj_set_parameter_logging_level(void* encoder, int logging) {
  return error_Ok;
}

struct heif_error opj_get_parameter_logging_level(void* encoder, int* logging) {
  return error_Ok;
}

const struct heif_encoder_parameter** opj_list_parameters(void* encoder) {
  return nullptr;
}

struct heif_error opj_set_parameter_integer(void* encoder, const char* name, int value) {
  return error_Ok;
}

struct heif_error opj_get_parameter_integer(void* encoder, const char* name, int* value) {
  return error_Ok;
}

struct heif_error opj_set_parameter_boolean(void* encoder, const char* name, int value) {
  return error_Ok;
}

struct heif_error opj_get_parameter_boolean(void* encoder, const char* name, int* value) {
  return error_Ok;
}

struct heif_error opj_set_parameter_string(void* encoder, const char* name, const char* value) {
  return error_Ok;
}

struct heif_error opj_get_parameter_string(void* encoder, const char* name, char* value, int value_size) {
  return error_Ok;
}

void opj_query_input_colorspace(enum heif_colorspace* inout_colorspace, enum heif_chroma* inout_chroma) {
  //TODO
  // Replace the input colorspace/chroma with the one that is supported by the encoder and that
  // comes as close to the input colorspace/chroma as possible.
}

static OPJ_SIZE_T global_variable = 0;
static OPJ_UINT64 opj_get_data_length_from_buffer(void * p_user_data, uint32_t size)
{
  printf("LINE: %d  FILE: %s  size: %x\n", __LINE__, __FILE__, size);
#  define OPJ_FSEEK(stream,offset,whence) fseek(stream,offset,whence)
#  define OPJ_FTELL(stream) ftell(stream)

    FILE* p_file = (FILE*)p_user_data;
    OPJ_OFF_T file_length = 0;

    OPJ_FSEEK(p_file, 0, SEEK_END);
    file_length = (OPJ_OFF_T)OPJ_FTELL(p_file);
    OPJ_FSEEK(p_file, 0, SEEK_SET);

    return (OPJ_UINT64)file_length;
}
static OPJ_SIZE_T opj_write_from_buffer(void * source, OPJ_SIZE_T nb_bytes, void * dest)
{
    printf("%s()  - nb_bytes: %ld = 0x%lx\n", __FUNCTION__, nb_bytes, nb_bytes);
    memcpy(dest, source, nb_bytes);
    global_variable += nb_bytes;
    return nb_bytes;
}
static void opj_close_from_buffer(void* p_user_data)
{
    FILE* p_file = (FILE*)p_user_data;
    fclose(p_file);
}

#define WRITE_JP2 0
static opj_image_t *to_opj_image(const unsigned char *buf, 
                                 int width, int height, 
                                 int nr_comp, int sub_dx, 
                                 int sub_dy) {
	const unsigned char *cs;
	opj_image_t *image;
	int *r, *g, *b, *a;
	int has_rgb, comp;
	unsigned int i, max;
	opj_image_cmptparm_t cmptparm[4];

	memset(&cmptparm, 0, 4 * sizeof(opj_image_cmptparm_t));

	for (comp = 0; comp < nr_comp; ++comp) {
    cmptparm[comp].prec = 8;
    cmptparm[comp].bpp = 8;
    cmptparm[comp].sgnd = 0;
    cmptparm[comp].dx = sub_dx;
    cmptparm[comp].dy = sub_dy;
    cmptparm[comp].w = width;
    cmptparm[comp].h = height;
   }

  {
	  OPJ_COLOR_SPACE csp = (nr_comp > 2?OPJ_CLRSPC_SRGB:OPJ_CLRSPC_GRAY);
	  image = opj_image_create(nr_comp, &cmptparm[0], csp);
  }

	if (image == NULL) {
	  fprintf(stderr, "%d: got no image\n",__LINE__);
	  return NULL;
  }

  image->x0 = 0;
  image->y0 = 0;
  image->x1 = (width - 1) * sub_dx + 1;
  image->y1 = (height - 1) * sub_dy + 1;

	r = g = b = a = NULL; 


	if (nr_comp == 3) {
    has_rgb = 1;
    r = image->comps[0].data;
    g = image->comps[1].data;
    b = image->comps[2].data;
  }
	else { //Grayscale
	  r = image->comps[0].data;
  }

	cs = buf;
	max = height * width;
	for (i = 0; i < max; ++i) {
	  if (has_rgb) {
      *r++ = (int)*cs++; 
      *g++ = (int)*cs++; 
      *b++ = (int)*cs++;
      continue;
    }

    /* G */
  	*r++ = (int)*cs++;
   }

	return image;

} 


static vector<uint8_t> generate_codestream(const uint8_t* data, uint32_t width, uint32_t  height, uint32_t  numcomps) {


  //Purpose - Generate a .j2k compressed file named "write_idf" from the input buffer. 
	opj_cparameters_t parameters;
	opj_image_t *image;
  int sub_dx, sub_dy;


	opj_set_default_encoder_parameters(&parameters);

	if(parameters.cp_comment == NULL)
   {
	char buf[80];
#ifdef _WIN32
	sprintf_s(buf, 80, "Created by OpenJPEG version %s", opj_version());
#else
	snprintf(buf, 80, "Created by OpenJPEG version %s", opj_version());
#endif
	parameters.cp_comment = strdup(buf);
   }

    if(parameters.tcp_numlayers == 0)
   {
    parameters.tcp_rates[0] = 0;   /* MOD antonin : losslessbug */
    parameters.tcp_numlayers++;
    parameters.cp_disto_alloc = 1;
   }
	sub_dx = parameters.subsampling_dx;
	sub_dy = parameters.subsampling_dy;

//--------------------------------------------------------
	image = to_opj_image(data, (int)width, (int)height,
		(int)numcomps, sub_dx, sub_dy);
  opj_image_comp_t r = image->comps[0];

//--------------------------------------------------------

	opj_stream_t *stream;
	opj_codec_t* codec;

	stream = NULL;	
	parameters.tcp_mct = image->numcomps == 3 ? 1 : 0;

	OPJ_CODEC_FORMAT codec_format = OPJ_CODEC_J2K;
	// OPJ_CODEC_FORMAT codec_format = OPJ_CODEC_JP2;
	codec = opj_create_compress(codec_format);

	opj_setup_encoder(codec, &parameters, image);

	// stream = opj_stream_create_default_file_stream(write_idf, WRITE_JP2);
  size_t size = width * height * numcomps;
  uint8_t* out_data = (uint8_t*) malloc(size);
  {
    opj_stream_t* l_stream = 00;

    l_stream = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, WRITE_JP2);

    opj_stream_set_user_data(l_stream, out_data, opj_close_from_buffer);
    // opj_stream_set_user_data_length(l_stream, 0);
    // opj_stream_set_read_function(l_stream, opj_read_from_buffer);
    opj_stream_set_write_function(l_stream, (opj_stream_write_fn) opj_write_from_buffer);
    // opj_stream_set_skip_function(l_stream, opj_skip_from_buffer);
    // opj_stream_set_seek_function(l_stream, opj_seek_from_buffer);

    // typedef struct opj_codestream_info 
    // typedef struct opj_codestream_index
    //    - codestream_size

    stream = l_stream;
  }

  vector<uint8_t> codestream;
	if(stream == NULL) {
    return codestream; //TODO - Handle errors properly
  }


	if( !opj_start_compress(codec, image, stream)) {
    return codestream; //TODO - Handle errors properly
  }

	if( !opj_encode(codec, stream)) {
    return codestream; //TODO - Handle errors properly
  }

	opj_end_compress(codec, stream);

  for (OPJ_SIZE_T i = 0; i < global_variable; i++) {
    codestream.push_back(out_data[i]);
  }

  return codestream;


}

struct heif_error opj_encode_image(void* encoder_raw, const struct heif_image* image, enum heif_image_input_class image_class) {
  
  struct encoder_struct_opj* encoder = (struct encoder_struct_opj*) encoder_raw;
  heif_chroma chroma = heif_image_get_chroma_format(image);
  heif_colorspace colorspace = heif_image_get_colorspace(image);
  struct heif_error err;

  uint32_t numcomps;
  heif_channel channel;
  if (chroma == heif_chroma_interleaved_RGB) {
    channel = heif_channel_interleaved;
    numcomps = 3;
  } 
  else {
    err = { heif_error_Unsupported_feature, 
            heif_suberror_Unsupported_data_version, 
            "Chroma not yet supported"};
    return err;
  }

  if (colorspace == heif_colorspace_RGB) {
    //
  }
  else {
    err = { heif_error_Unsupported_feature, 
            heif_suberror_Unsupported_data_version,
            "Colorspace not yet supported"};
    return err;
  }



  //GET PIXEL DATA
  int stride = 0;
  const uint8_t* src_data = heif_image_get_plane_readonly(image, channel, &stride);

  //GET CODESTREAM
  unsigned int width = heif_image_get_primary_width(image);
  unsigned int height = heif_image_get_primary_height(image);
  // vector<uint8_t> codestream = generate_codestream(src_data, width, height, numcomps);
  encoder->compressedData.clear(); //Fixes issue when encoding multiple images and old data persists.
  encoder->compressedData = generate_codestream(src_data, width, height, numcomps);

  // for (size_t i = 0; i < codestream.size(); i++) {
  //   uint8_t x = codestream[i];
  //   encoder->compressedData.push_back(x);
  // }
  // // encoder->compressedData.

  return error_Ok;
}

struct heif_error opj_get_compressed_data(void* encoder_raw, uint8_t** data, int* size, enum heif_encoded_data_type* type) {
  // Get a packet of decoded data. The data format depends on the codec.
  
  struct encoder_struct_opj* encoder = (struct encoder_struct_opj*) encoder_raw;

  if (encoder->data_read) {
    *size = 0;
    *data = nullptr;
  } 
  else {
    *size = (int) encoder->compressedData.size();
    *data = encoder->compressedData.data();
    encoder->data_read = true;
  }

  return error_Ok;
}

void opj_query_input_colorspace2(void* encoder, enum heif_colorspace* inout_colorspace, enum heif_chroma* inout_chroma) {
  //TODO
}

void opj_query_encoded_size(void* encoder, uint32_t input_width, uint32_t input_height, uint32_t* encoded_width, uint32_t* encoded_height) {
  // --- version 3 ---

  // The encoded image size may be different from the input frame size, e.g. because
  // of required rounding, or a required minimum size. Use this function to return
  // the encoded size for a given input image size.
  // You may set this to NULL if no padding is required for any image size.
}


static const struct heif_encoder_plugin encoder_plugin_openjpeg
    {
        /* plugin_api_version */ 3,
        /* compression_format */ heif_compression_JPEG2000,
        /* id_name */ "OpenJPEG",
        /* priority */ OPJ_PLUGIN_PRIORITY,
        /* supports_lossy_compression */ false,
        /* supports_lossless_compression */ true,
        /* get_plugin_name */ opj_plugin_name,
        /* init_plugin */ opj_init_plugin,
        /* cleanup_plugin */ opj_cleanup_plugin,
        /* new_encoder */ opj_new_encoder,
        /* free_encoder */ opj_free_encoder,
        /* set_parameter_quality */ opj_set_parameter_quality,
        /* get_parameter_quality */ opj_get_parameter_quality,
        /* set_parameter_lossless */ opj_set_parameter_lossless,
        /* get_parameter_lossless */ opj_get_parameter_lossless,
        /* set_parameter_logging_level */ opj_set_parameter_logging_level,
        /* get_parameter_logging_level */ opj_get_parameter_logging_level,
        /* list_parameters */ opj_list_parameters,
        /* set_parameter_integer */ opj_set_parameter_integer,
        /* get_parameter_integer */ opj_get_parameter_integer,
        /* set_parameter_boolean */ opj_set_parameter_boolean,
        /* get_parameter_boolean */ opj_get_parameter_boolean,
        /* set_parameter_string */ opj_set_parameter_string,
        /* get_parameter_string */ opj_get_parameter_string,
        /* query_input_colorspace */ opj_query_input_colorspace,
        /* encode_image */ opj_encode_image,
        /* get_compressed_data */ opj_get_compressed_data,
        /* query_input_colorspace (v2) */ opj_query_input_colorspace2,
        /* query_encoded_size (v3) */ opj_query_encoded_size
    };

const struct heif_encoder_plugin* get_encoder_plugin_openjpeg() {
    return &encoder_plugin_openjpeg;
}
