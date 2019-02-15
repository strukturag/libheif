AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = \
  heif-convert \
  heif-enc \
  heif-info

noinst_PROGRAMS = \
  heif-test \
  test-c-api

heif_convert_DEPENDENCIES = ../libheif/libheif.la
heif_convert_CXXFLAGS = -I$(top_srcdir) -I$(top_builddir)/.
heif_convert_LDFLAGS =
heif_convert_LDADD = ../libheif/libheif.la
heif_convert_SOURCES = encoder.cc encoder.h heif_convert.cc encoder_y4m.cc encoder_y4m.h


if HAVE_LIBPNG
bin_PROGRAMS += heif-thumbnailer
heif_thumbnailer_DEPENDENCIES = ../libheif/libheif.la
heif_thumbnailer_CXXFLAGS = -I$(top_srcdir) -I$(top_builddir) $(libpng_CFLAGS)
heif_thumbnailer_LDFLAGS = $(libpng_LIBS)
heif_thumbnailer_LDADD = ../libheif/libheif.la
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

heif_info_DEPENDENCIES = ../libheif/libheif.la
heif_info_CXXFLAGS = -I$(top_srcdir) -I$(top_builddir)
heif_info_LDFLAGS =
heif_info_LDADD = ../libheif/libheif.la
heif_info_SOURCES = heif_info.cc

heif_enc_DEPENDENCIES = ../libheif/libheif.la
heif_enc_CXXFLAGS = -I$(top_srcdir) -I$(top_builddir)
heif_enc_LDFLAGS =
heif_enc_LDADD = ../libheif/libheif.la
heif_enc_SOURCES = heif_enc.cc

if HAVE_LIBJPEG
heif_enc_CXXFLAGS += $(libjpeg_CFLAGS)
heif_enc_LDADD += $(libjpeg_LIBS)
endif

if HAVE_LIBPNG
heif_enc_CXXFLAGS += $(libpng_CFLAGS)
heif_enc_LDADD += $(libpng_LIBS)
endif

heif_test_DEPENDENCIES = ../libheif/libheif.la
heif_test_CXXFLAGS = -I$(top_srcdir) -I$(top_builddir)
heif_test_LDFLAGS =
heif_test_LDADD = ../libheif/libheif.la
heif_test_SOURCES = heif_test.cc

test_c_api_DEPENDENCIES = ../libheif/libheif.la
test_c_api_CFLAGS = -I$(top_srcdir) -I$(top_builddir)
test_c_api_LDFLAGS =
test_c_api_LDADD = ../libheif/libheif.la
test_c_api_SOURCES = test_c_api.c

EXTRA_DIST = \
    CMakeLists.txt \
    COPYING \
    demo.html \
    example.heic

if HAVE_GO
noinst_PROGRAMS += \
	heif-test-go

heif_test_go_SOURCES = heif-test.go

gopath:
	mkdir -p ${CURDIR}/src/github.com/strukturag/libheif
	ln -sf ${CURDIR}/../go ${CURDIR}/src/github.com/strukturag/libheif/

heif-test-go: gopath $(top_builddir)/libheif/libheif.la $(top_builddir)/libheif.pc heif-test.go
	GOPATH=${CURDIR} PKG_CONFIG_PATH=$(abs_top_builddir) CGO_CFLAGS="-I$(abs_top_builddir)" CGO_LDFLAGS="-L$(abs_top_builddir)/libheif/.libs" LD_LIBRARY_PATH=$(abs_top_builddir)/libheif/.libs $(GO) build -o heif-test-go ${heif_test_go_SOURCES}

format-go: ${heif_test_go_SOURCES}
	$(GO) fmt ${heif_test_go_SOURCES}
else
format-go:
	echo ""go" not present in "${PATH}", skipping formatting"
endif

format-local: format-go
