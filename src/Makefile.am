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
libheif_la_LIBADD =

libheif_la_LDFLAGS = -version-info $(LIBHEIF_CURRENT):$(LIBHEIF_REVISION):$(LIBHEIF_AGE)

libheif_la_SOURCES = \
  box.h \
  box.cc

libheif_la_HEADERS = \
  box.h \
  bitstream.h \
  error.h \
  logging.h

if HAVE_LIBDE265
libheif_la_SOURCES += \
  heif_file.h \
  heif_file.cc

libheif_la_HEADERS += \
  heif_file.h

libheif_la_LIBADD += \
  $(libde265_LIBS)
endif

if ENABLE_LIBFUZZER
bin_PROGRAMS += libheif-fuzzer
endif

libheif_fuzzer_DEPENDENCIES =
libheif_fuzzer_CXXFLAGS =
libheif_fuzzer_LDFLAGS = -fsanitize=fuzzer
libheif_fuzzer_LDADD =
libheif_fuzzer_SOURCES = $(libheif_la_SOURCES) fuzzer.cc
if HAVE_LIBDE265
libheif_fuzzer_LDADD += \
  $(libde265_LIBS)
endif
