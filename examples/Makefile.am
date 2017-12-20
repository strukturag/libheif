AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
  convert \
  heif

convert_DEPENDENCIES = ../src/libheif.la
convert_CXXFLAGS = -I../src $(libde265_CFLAGS)
convert_LDFLAGS =
convert_LDADD = ../src/libheif.la $(libde265_LIBS)
convert_SOURCES = encoder.h convert.cc

if HAVE_LIBJPEG
convert_CXXFLAGS += $(libjpeg_CFLAGS)
convert_LDADD += $(libjpeg_LIBS)
convert_SOURCES += encoder_jpeg.cc encoder_jpeg.h
endif

if HAVE_LIBPNG
convert_CXXFLAGS += $(libpng_CFLAGS)
convert_LDADD += $(libpng_LIBS)
convert_SOURCES += encoder_png.cc encoder_png.h
endif

heif_DEPENDENCIES = ../src/libheif.la
heif_CXXFLAGS = -I../src $(libde265_CFLAGS)
heif_LDFLAGS =
heif_LDADD = ../src/libheif.la $(libde265_LIBS)
heif_SOURCES = heif.cc
