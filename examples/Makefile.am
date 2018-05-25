AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
  heif-convert \
  heif-enc \
  heif-info

noinst_PROGRAMS = \
  heif-test

heif_convert_DEPENDENCIES = ../src/libheif.la
heif_convert_CXXFLAGS = -I$(top_srcdir)/src -I$(top_builddir)/src
heif_convert_LDFLAGS =
heif_convert_LDADD = ../src/libheif.la
heif_convert_SOURCES = encoder.cc encoder.h heif_convert.cc


if HAVE_LIBPNG
bin_PROGRAMS += heif-thumbnailer
heif_thumbnailer_DEPENDENCIES = ../src/libheif.la
heif_thumbnailer_CXXFLAGS = -I$(top_srcdir)/src -I$(top_builddir)/src $(libpng_CFLAGS)
heif_thumbnailer_LDFLAGS = $(libpng_LIBS)
heif_thumbnailer_LDADD = ../src/libheif.la
heif_thumbnailer_SOURCES = encoder.cc encoder.h heif_thumbnailer.cc encoder_png.cc encoder_png.h
endif

if HAVE_LIBJPEG
heif_convert_CXXFLAGS += $(libjpeg_CFLAGS)
heif_convert_LDADD += $(libjpeg_LIBS)
heif_convert_SOURCES += encoder_jpeg.cc encoder_jpeg.h
endif

if HAVE_LIBPNG
heif_convert_CXXFLAGS += $(libpng_CFLAGS)
heif_convert_LDADD += $(libpng_LIBS)
heif_convert_SOURCES += encoder_png.cc encoder_png.h
endif

heif_info_DEPENDENCIES = ../src/libheif.la
heif_info_CXXFLAGS = -I$(top_srcdir)/src -I$(top_builddir)/src
heif_info_LDFLAGS =
heif_info_LDADD = ../src/libheif.la
heif_info_SOURCES = heif_info.cc

heif_enc_DEPENDENCIES = ../src/libheif.la
heif_enc_CXXFLAGS = -I$(top_srcdir)/src -I$(top_builddir)/src
heif_enc_LDFLAGS =
heif_enc_LDADD = ../src/libheif.la
heif_enc_SOURCES = heif_enc.cc

if HAVE_LIBJPEG
heif_enc_CXXFLAGS += $(libjpeg_CFLAGS)
heif_enc_LDADD += $(libjpeg_LIBS)
endif

if HAVE_LIBPNG
heif_enc_CXXFLAGS += $(libpng_CFLAGS)
heif_enc_LDADD += $(libpng_LIBS)
endif

heif_test_DEPENDENCIES = ../src/libheif.la
heif_test_CXXFLAGS = -I$(top_srcdir)/src -I$(top_builddir)/src
heif_test_LDFLAGS =
heif_test_LDADD = ../src/libheif.la
heif_test_SOURCES = heif_test.cc

EXTRA_DIST = \
    demo.html \
    example.heic
