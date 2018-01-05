AC_PREREQ([2.68])
AC_INIT([libheif], [0.0.1], [opensource@struktur.de])
AC_CONFIG_SRCDIR([src/box.cc])
AC_CONFIG_HEADERS([config.h])

LIBHEIF_CURRENT=1
LIBHEIF_REVISION=0
LIBHEIF_AGE=0
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

AM_INIT_AUTOMAKE([foreign])

AX_CXX_COMPILE_STDCXX_11()

AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_FUNC_ERROR_AT_LINE

AC_TYPE_SIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

AC_ARG_ENABLE([visibility], AS_HELP_STRING([--disable-visibility],
    [Disable visibility definitions.]),
    [HAVE_VISIBILITY=0],
    [gl_VISIBILITY])
AM_CONDITIONAL([HAVE_VISIBILITY], [test "x$HAVE_VISIBILITY" != "x0"])

PKG_CHECK_MODULES([libde265], [libde265], [
    AC_DEFINE([HAVE_LIBDE265], [1], [Whether libde265 was found.])
    AC_SUBST(libde265_CFLAGS)
    AC_SUBST(libde265_LIBS)
    have_libde265="yes"
], [have_libde265="no"])
AM_CONDITIONAL([HAVE_LIBDE265], [test "x$have_libde265" = "xyes"])

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

AM_CONDITIONAL([ENABLE_EXAMPLES], [test "x$have_libde265" != "xno"])
AC_MSG_NOTICE([Building examples: $have_libde265])

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

CXXFLAGS="$CXXFLAGS -Wall -Werror -Wsign-compare"
CFLAGS="$CFLAGS -Wall -Werror -Wsign-compare"

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([examples/Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([libheif.pc])
AC_OUTPUT
