AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = heif

heif_DEPENDENCIES = ../src/libheif.la
heif_CXXFLAGS = -I../src $(libde265_CFLAGS)
heif_LDFLAGS =
heif_LDADD = ../src/libheif.la $(libde265_LIBS)
heif_SOURCES = heif.cc
