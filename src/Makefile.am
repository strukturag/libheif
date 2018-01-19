AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS =
lib_LTLIBRARIES =

lib_LTLIBRARIES += libheif.la

libheif_ladir = \
        $(includedir)/libheif

libheif_la_CPPFLAGS =
libheif_la_CFLAGS = \
  $(CFLAG_VISIBILITY) \
  -DLIBHEIF_EXPORTS
libheif_la_CXXFLAGS = \
  $(CFLAG_VISIBILITY) \
  $(libde265_CFLAGS) \
  -DLIBHEIF_EXPORTS
libheif_la_LIBADD = $(libde265_LIBS)

libheif_la_LDFLAGS = -version-info $(LIBHEIF_CURRENT):$(LIBHEIF_REVISION):$(LIBHEIF_AGE)

libheif_la_SOURCES = \
  bitstream.h \
  bitstream.cc \
  box.h \
  box.cc \
  error.h \
  error.cc \
  heif_api_structs.h \
  heif_file.h \
  heif_file.cc \
  heif_image.h \
  heif_image.cc \
  heif.h \
  heif.cc \
  heif_context.h \
  heif_context.cc \
  logging.h

if HAVE_LIBDE265
libheif_la_SOURCES += \
  heif_decoder_libde265.cc \
  heif_decoder_libde265.h
endif

libheif_la_HEADERS = \
  heif.h \
  heif-version.h

noinst_HEADERS = \
  heif-emscripten.h

if HAVE_VISIBILITY
libheif_la_CFLAGS += -DHAVE_VISIBILITY
libheif_la_CXXFLAGS += -DHAVE_VISIBILITY
endif

if ENABLE_LIBFUZZER
bin_PROGRAMS += box-fuzzer
if HAVE_LIBDE265
bin_PROGRAMS += file-fuzzer
endif
else
noinst_LIBRARIES = libfuzzers.a
libfuzzers_a_SOURCES = \
  box-fuzzer.cc \
  file-fuzzer.cc
endif

box_fuzzer_DEPENDENCIES =
box_fuzzer_CXXFLAGS =
box_fuzzer_LDFLAGS = -fsanitize=fuzzer
box_fuzzer_LDADD =
box_fuzzer_SOURCES = $(libheif_la_SOURCES) box-fuzzer.cc

if HAVE_LIBDE265
box_fuzzer_LDADD += $(libde265_LIBS)
endif

file_fuzzer_DEPENDENCIES =
file_fuzzer_CXXFLAGS =
file_fuzzer_LDFLAGS = -fsanitize=fuzzer
file_fuzzer_LDADD =
file_fuzzer_SOURCES = $(libheif_la_SOURCES) file-fuzzer.cc

if HAVE_LIBDE265
file_fuzzer_LDADD += $(libde265_LIBS)
endif
