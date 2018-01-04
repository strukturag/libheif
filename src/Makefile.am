AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS =
lib_LTLIBRARIES =

lib_LTLIBRARIES += libheif.la

libheif_ladir = \
        $(includedir)/libheif

libheif_la_CPPFLAGS =
libheif_la_CFLAGS = \
  $(CFLAG_VISIBILITY)
libheif_la_CXXFLAGS = \
  $(CFLAG_VISIBILITY) $(libde265_CFLAGS)
libheif_la_LIBADD = $(libde265_LIBS)

libheif_la_LDFLAGS = -version-info $(LIBHEIF_CURRENT):$(LIBHEIF_REVISION):$(LIBHEIF_AGE)

libheif_la_SOURCES = \
  bitstream.h \
  box.h \
  box.cc \
  error.h \
  heif_file.h \
  heif_file.cc \
  heif_image.h \
  heif_image.cc \
  heif_decoder_libde265.cc \
  heif.h \
  heif.cc \
  heif_context.h \
  heif_context.cc \
  logging.h

libheif_la_HEADERS = \
  heif.h

if ENABLE_LIBFUZZER
bin_PROGRAMS += box-fuzzer
if HAVE_LIBDE265
bin_PROGRAMS += file-fuzzer
endif
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
