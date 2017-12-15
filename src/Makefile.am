AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS =
lib_LTLIBRARIES =

if !ENABLE_LIBFUZZER
lib_LTLIBRARIES += libheif.la
endif

libheif_ladir = \
        $(includedir)/libheif

libheif_la_CPPFLAGS =
libheif_la_CFLAGS = \
  $(CFLAG_VISIBILITY)
libheif_la_CXXFLAGS = \
  $(CFLAG_VISIBILITY)

libheif_la_LDFLAGS = -version-info $(LIBHEIF_CURRENT):$(LIBHEIF_REVISION):$(LIBHEIF_AGE)

libheif_la_SOURCES = \
  box.h \
  box.cc

libheif_la_HEADERS = \
  box.h

if ENABLE_LIBFUZZER
bin_PROGRAMS += libheif-fuzzer
endif

libheif_fuzzer_DEPENDENCIES =
libheif_fuzzer_CXXFLAGS =
libheif_fuzzer_LDFLAGS =
libheif_fuzzer_LDADD =
libheif_fuzzer_SOURCES = $(libheif_la_SOURCES) fuzzer.cc
