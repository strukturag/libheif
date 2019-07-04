
noinst_PROGRAMS = \
  heif-unit-tests

heif_unit_tests_DEPENDENCIES = ../libheif/libheif.la
heif_unit_tests_CXXFLAGS = -I$(top_srcdir) -I$(top_builddir)/.
heif_unit_tests_LDFLAGS =
heif_unit_tests_LDADD = ../libheif/libheif.la
heif_unit_tests_SOURCES = main.cc encode.cc
