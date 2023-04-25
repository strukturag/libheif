AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS =
lib_LTLIBRARIES =

lib_LTLIBRARIES += libheif.la

libheif_ladir = \
        $(includedir)/libheif

ADDITIONAL_LIBS =
if HAVE_AOM
ADDITIONAL_LIBS += $(aom_LIBS)
endif

if HAVE_LIBDE265
ADDITIONAL_LIBS += $(libde265_LIBS)
endif

if HAVE_X265
ADDITIONAL_LIBS += $(x265_LIBS)
endif

if HAVE_RAV1E
ADDITIONAL_LIBS += $(rav1e_LIBS)
endif

if HAVE_DAV1D
ADDITIONAL_LIBS += $(dav1d_LIBS)
endif

if HAVE_SvtEnc
ADDITIONAL_LIBS += $(svt_LIBS)
endif

if HAVE_LIBSHARPYUV
ADDITIONAL_LIBS += $(libsharpyuv_LIBS)
endif

libheif_la_CPPFLAGS =
libheif_la_CFLAGS = \
  $(CFLAG_VISIBILITY) \
  -DLIBHEIF_EXPORTS
libheif_la_CXXFLAGS = \
  $(CFLAG_VISIBILITY) \
  $(aom_CFLAGS) \
  $(libde265_CFLAGS) \
  $(x265_CFLAGS) \
  $(dav1d_CFLAGS) \
  $(rav1e_CFLAGS) \
  $(svt_CFLAGS) \
  $(libsharpyuv_CFLAGS) \
  -DLIBHEIF_EXPORTS -I$(top_srcdir)
libheif_la_LIBADD = $(ADDITIONAL_LIBS)

libheif_la_LDFLAGS = -version-info $(LIBHEIF_CURRENT):$(LIBHEIF_REVISION):$(LIBHEIF_AGE)
if MINGW
libheif_la_LDFLAGS += -no-undefined
endif

libheif_la_SOURCES = \
  bitstream.h \
  bitstream.cc \
  color-conversion/colorconversion.cc \
  color-conversion/colorconversion.h \
  color-conversion/rgb2yuv.cc \
  color-conversion/rgb2yuv.h \
  color-conversion/rgb2yuv_sharp.cc \
  color-conversion/rgb2yuv_sharp.h \
  color-conversion/yuv2rgb.cc \
  color-conversion/yuv2rgb.h \
  color-conversion/rgb2rgb.cc \
  color-conversion/rgb2rgb.h \
  color-conversion/monochrome.cc \
  color-conversion/monochrome.h \
  color-conversion/hdr_sdr.cc \
  color-conversion/hdr_sdr.h \
  color-conversion/alpha.cc \
  color-conversion/alpha.h \
  box.h \
  box.cc \
  error.h \
  error.cc \
  heif_api_structs.h \
  heif_file.h \
  heif_file.cc \
  heif_image.h \
  heif_image.cc \
  heif_limits.h \
  heif.h \
  heif.cc \
  heif_context.h \
  heif_context.cc \
  heif_hevc.h \
  heif_hevc.cc \
  heif_init.h \
  heif_init.cc \
  heif_avif.h \
  heif_avif.cc \
  heif_plugin_registry.h \
  heif_plugin_registry.cc \
  heif_plugin.h \
  heif_plugin.cc \
  logging.h \
  mask_image.h \
  mask_image.cc \
  metadata_compression.h \
  metadata_compression.cc \
  nclx.cc \
  nclx.h \
  common_utils.cc \
  common_utils.h \
  region.h \
  region.cc

if HAVE_AOM_DECODER
libheif_la_SOURCES += \
  plugins/heif_decoder_aom.cc \
  plugins/heif_decoder_aom.h
endif

if HAVE_AOM_ENCODER
libheif_la_SOURCES += \
  plugins/heif_encoder_aom.cc \
  plugins/heif_encoder_aom.h
endif

if HAVE_LIBDE265
libheif_la_SOURCES += \
  plugins/heif_decoder_libde265.cc \
  plugins/heif_decoder_libde265.h
endif

if HAVE_RAV1E
libheif_la_SOURCES += \
  plugins/heif_encoder_rav1e.cc \
  plugins/heif_encoder_rav1e.h
endif

if HAVE_DAV1D
libheif_la_SOURCES += \
  plugins/heif_decoder_dav1d.cc \
  plugins/heif_decoder_dav1d.h
endif

if HAVE_SvtEnc
libheif_la_SOURCES += \
  plugins/heif_encoder_svt.cc \
  plugins/heif_encoder_svt.h
endif


if HAVE_X265
libheif_la_SOURCES += \
  plugins/heif_encoder_x265.cc \
  plugins/heif_encoder_x265.h
endif

if ENABLE_UNCOMPRESSED
libheif_la_SOURCES += \
  uncompressed_image.cc \
  uncompressed_image.h \
  plugins/heif_encoder_uncompressed.cc \
  plugins/heif_encoder_uncompressed.h
endif

libheif_la_HEADERS = \
  heif.h \
  heif_plugin.h \
  heif_version.h \
  heif_cxx.h

noinst_HEADERS = \
  heif_emscripten.h

if HAVE_VISIBILITY
libheif_la_CFLAGS += -DHAVE_VISIBILITY
libheif_la_CXXFLAGS += -DHAVE_VISIBILITY
endif

if ENABLE_LIBFUZZER
bin_PROGRAMS += box-fuzzer \
  color-conversion-fuzzer
if HAVE_LIBDE265
bin_PROGRAMS += file-fuzzer
endif
if HAVE_X265
bin_PROGRAMS += encoder-fuzzer
endif
else
noinst_LIBRARIES = libfuzzers.a
libfuzzers_a_SOURCES = \
  box_fuzzer.cc \
  color_conversion_fuzzer.cc \
  encoder_fuzzer.cc \
  file_fuzzer.cc
endif

box_fuzzer_DEPENDENCIES =
box_fuzzer_CXXFLAGS = $(libde265_CFLAGS)
box_fuzzer_LDFLAGS = $(FUZZING_ENGINE)
box_fuzzer_LDADD = $(ADDITIONAL_LIBS)
box_fuzzer_SOURCES = $(libheif_la_SOURCES) box_fuzzer.cc

color_conversion_fuzzer_DEPENDENCIES =
color_conversion_fuzzer_CXXFLAGS = $(libde265_CFLAGS)
color_conversion_fuzzer_LDFLAGS = $(FUZZING_ENGINE)
color_conversion_fuzzer_LDADD = $(ADDITIONAL_LIBS)
color_conversion_fuzzer_SOURCES = $(libheif_la_SOURCES) color_conversion_fuzzer.cc

encoder_fuzzer_DEPENDENCIES =
encoder_fuzzer_CXXFLAGS = $(libde265_CFLAGS)
encoder_fuzzer_LDFLAGS = $(FUZZING_ENGINE)
encoder_fuzzer_LDADD = $(ADDITIONAL_LIBS)
encoder_fuzzer_SOURCES = $(libheif_la_SOURCES) encoder_fuzzer.cc

file_fuzzer_DEPENDENCIES =
file_fuzzer_CXXFLAGS = $(libde265_CFLAGS)
file_fuzzer_LDFLAGS = $(FUZZING_ENGINE)
file_fuzzer_LDADD = $(ADDITIONAL_LIBS)
file_fuzzer_SOURCES = $(libheif_la_SOURCES) file_fuzzer.cc

# hack, plugin_*.h files are not used by autotools, because our autotools build scripts don't support plugins, but we need it anyways
EXTRA_DIST = \
  CMakeLists.txt \
  plugins/CMakeLists.txt \
  plugins_unix.h \
  plugins_unix.cc \
  plugins_windows.h \
  plugins_windows.cc
