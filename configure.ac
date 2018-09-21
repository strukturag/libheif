AC_PREREQ([2.68])
AC_INIT([libheif], [1.4.0], [opensource@struktur.de])
AC_CONFIG_SRCDIR([libheif/box.cc])
AC_CONFIG_HEADERS([config.h])

NUMERIC_VERSION=0x01040000 # Numeric representation of the version (A.B.C[.D] = 0xAABBCCDD)
AC_SUBST(NUMERIC_VERSION)

# From https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html:
# If the library source code has changed at all since the last update, then increment revision (‘c:r:a’ becomes ‘c:r+1:a’).
# If any interfaces have been added, removed, or changed since the last update, increment current, and set revision to 0.
# If any interfaces have been added since the last public release, then increment age.
# If any interfaces have been removed or changed since the last public release, then set age to 0.

LIBHEIF_CURRENT=5
LIBHEIF_REVISION=0
LIBHEIF_AGE=4
AC_SUBST(LIBHEIF_CURRENT)
AC_SUBST(LIBHEIF_REVISION)
AC_SUBST(LIBHEIF_AGE)

AC_CANONICAL_SYSTEM
LT_INIT
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.])

# Checks for programs.
AM_PROG_AS
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_GREP
AC_ARG_ENABLE([go], AS_HELP_STRING([--disable-go],
    [Disable building Go code / examples.]))
if eval "test x$enable_go = x" ; then
    AC_PATH_PROG([GO], [go], [no], [$PATH$PATH_SEPARATOR/usr/lib/go-1.10$PATH_SEPARATOR/usr/lib/go-1.9$PATH_SEPARATOR/usr/lib/go-1.8$PATH_SEPARATOR/usr/lib/go-1.7$PATH_SEPARATOR/usr/lib/go-1.6])
    AC_SUBST(GO)
fi
AM_CONDITIONAL([HAVE_GO], [test "x$GO" != "xno" && test "x$GO" != "x"])
AC_SUBST(HAVE_GO)

AM_CONDITIONAL(MINGW, expr $host : '.*-mingw' >/dev/null 2>&1)

AM_INIT_AUTOMAKE([foreign])
AM_EXTRA_RECURSIVE_TARGETS([test])

AX_CXX_COMPILE_STDCXX_11()

AC_HEADER_STDBOOL
AC_CHECK_HEADERS([inttypes.h stddef.h unistd.h])
AC_C_INLINE
AC_FUNC_ERROR_AT_LINE

AC_TYPE_SIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

AC_CHECK_LIB([pthread], [pthread_create])

AC_ARG_ENABLE([visibility], AS_HELP_STRING([--disable-visibility],
    [Disable visibility definitions.]),
    [HAVE_VISIBILITY=0],
    [gl_VISIBILITY])
AM_CONDITIONAL([HAVE_VISIBILITY], [test "x$HAVE_VISIBILITY" != "x0"])
if eval "test x$enable_visibility = x" ; then enable_visibility=yes ; fi

PKG_CHECK_MODULES([libde265], [libde265], [
    AC_DEFINE([HAVE_LIBDE265], [1], [Whether libde265 was found.])
    AC_SUBST(libde265_CFLAGS)
    AC_SUBST(libde265_LIBS)
    have_libde265="yes"
], [have_libde265="no"])
AM_CONDITIONAL([HAVE_LIBDE265], [test "x$have_libde265" = "xyes"])
AC_SUBST([have_libde265])

PKG_CHECK_MODULES([x265], [x265], [
    AC_DEFINE([HAVE_X265], [1], [Whether x265 was found.])
    AC_SUBST(x265_CFLAGS)
    AC_SUBST(x265_LIBS)
    have_x265="yes"
], [have_x265="no"])
AM_CONDITIONAL([HAVE_X265], [test "x$have_x265" = "xyes"])
AC_SUBST([have_x265])

AC_CHECK_HEADERS([jpeglib.h])
AC_CHECK_LIB([jpeg], [jpeg_CreateCompress], [
    AC_DEFINE([HAVE_LIBJPEG], [1], [Whether libjpeg was found.])
    libjpeg_CFLAGS=""
    AC_SUBST(libjpeg_CFLAGS)
    libjpeg_LIBS="-ljpeg"
    AC_SUBST(libjpeg_LIBS)
    have_libjpeg="yes"
], [have_libjpeg="no"])
if eval "test x$have_libjpeg = xno"; then
AC_CHECK_LIB([jpeg], [jpeg_destroy_compress], [
    AC_DEFINE([HAVE_LIBJPEG], [1], [Whether libjpeg was found.])
    libjpeg_CFLAGS=""
    AC_SUBST(libjpeg_CFLAGS)
    libjpeg_LIBS="-ljpeg"
    AC_SUBST(libjpeg_LIBS)
    have_libjpeg="yes"
], [have_libjpeg="no"])
fi
AM_CONDITIONAL([HAVE_LIBJPEG], [test "x$have_libjpeg" = "xyes"])

PKG_CHECK_MODULES([libpng], [libpng], [
    AC_DEFINE([HAVE_LIBPNG], [1], [Whether libpng was found.])
    AC_SUBST(libpng_CFLAGS)
    AC_SUBST(libpng_LIBS)
    have_libpng="yes"
], [have_libpng="no"])
AM_CONDITIONAL([HAVE_LIBPNG], [test "x$have_libpng" = "xyes"])

AC_ARG_ENABLE([libfuzzer], AS_HELP_STRING([--enable-libfuzzer],
    [Enable fuzzing with libFuzzer.]))
if eval "test x$enable_libfuzzer = xyes"; then
    AC_MSG_NOTICE([Enable libFuzzer])
    AC_DEFINE([HAVE_LIBFUZZER], [1], [Whether building with libFuzzer.])
    LIBFUZZER_FLAGS="-fsanitize=fuzzer-no-link,address,shift,integer -fno-sanitize-recover=shift,integer"
    CFLAGS="$CFLAGS $LIBFUZZER_FLAGS"
    CXXFLAGS="$CXXFLAGS $LIBFUZZER_FLAGS"
fi
AM_CONDITIONAL([ENABLE_LIBFUZZER], [test "x$enable_libfuzzer" = "xyes"])
AC_SUBST(ENABLE_LIBFUZZER)

AC_ARG_ENABLE([multithreading], AS_HELP_STRING([--disable-multithreading],
    [Disable multithreaded decoding.]))
if eval "test x$enable_multithreading = x" ; then enable_multithreading=yes ; fi
if eval "test x$enable_multithreading != xno"; then
    AC_MSG_NOTICE([Enable multithreading])
    AC_DEFINE([ENABLE_PARALLEL_TILE_DECODING], [1], [Whether we enable multithreaded decoding.])
fi
AC_SUBST(ENABLE_PARALLEL_TILE_DECODING)

CXXFLAGS="$CXXFLAGS -Wall -Werror -Wsign-compare -Wconversion -Wno-sign-conversion -Wno-error=conversion -Wno-error=unused-parameter"
CFLAGS="$CFLAGS -Wall -Werror -Wsign-compare -Wconversion -Wno-sign-conversion -Wno-error=conversion -Wno-error=unused-parameter"

AC_MSG_NOTICE([---------------------------------------])
AC_MSG_NOTICE([Multithreading: $enable_multithreading])
AC_MSG_NOTICE([Symbol visibility: $enable_visibility])
AC_MSG_NOTICE([libde265 decoder: $have_libde265])
AC_MSG_NOTICE([libx265 encoder: $have_x265])
AC_MSG_NOTICE([JPEG output: $have_libjpeg])
AC_MSG_NOTICE([PNG output: $have_libpng])
AC_MSG_NOTICE([---------------------------------------])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([examples/Makefile])
AC_CONFIG_FILES([extra/Makefile])
AC_CONFIG_FILES([fuzzing/Makefile])
AC_CONFIG_FILES([go/Makefile])
AC_CONFIG_FILES([gnome/Makefile])
AC_CONFIG_FILES([scripts/Makefile])
AC_CONFIG_FILES([libheif/Makefile])
AC_CONFIG_FILES([libheif/heif_version.h])
AC_CONFIG_FILES([libheif.pc])
AC_OUTPUT
