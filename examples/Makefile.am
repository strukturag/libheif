AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
  heif-convert \
  heif-info

heif_convert_DEPENDENCIES = ../src/libheif.la
heif_convert_CXXFLAGS = -I../src
heif_convert_LDFLAGS =
heif_convert_LDADD = ../src/libheif.la
heif_convert_SOURCES = encoder.cc encoder.h heif_convert.cc

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
heif_info_CXXFLAGS = -I../src
heif_info_LDFLAGS =
heif_info_LDADD = ../src/libheif.la
heif_info_SOURCES = heif_info.cc


if ! HAVE_VISIBILITY
bin_PROGRAMS += heif-writer

heif_writer_DEPENDENCIES = ../src/libheif.la
heif_writer_CXXFLAGS = -I../src $(libjpeg_CFLAGS)
heif_writer_LDFLAGS =
heif_writer_LDADD = ../src/libheif.la $(x265_LIBS) $(libjpeg_LIBS)
heif_writer_SOURCES = heif_writer.cc
endif

EXTRA_DIST = \
    example.heic
