include(LibFindMacros)
libfind_pkg_check_modules(SvtEnc_PKGCONF SvtAv1Enc)

find_path(SvtEnc_INCLUDE_DIR
    NAMES svt-av1/EbSvtAv1Enc.h
    HINTS ${SvtEnc_PKGCONF_INCLUDE_DIRS} ${SvtEnc_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES SvtEnc
)

find_library(SvtEnc_LIBRARY
    NAMES SvtAv1Enc libSvtAv1Enc
    HINTS ${SvtEnc_PKGCONF_LIBRARY_DIRS} ${SvtEnc_PKGCONF_LIBDIR}
)

set(SvtEnc_PROCESS_LIBS SvtEnc_LIBRARY)
set(SvtEnc_PROCESS_INCLUDES SvtEnc_INCLUDE_DIR)
libfind_process(SvtEnc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SvtEnc
    REQUIRED_VARS
        SvtEnc_INCLUDE_DIR
        SvtEnc_LIBRARIES
)
