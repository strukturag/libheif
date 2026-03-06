include(LibFindMacros)
include(CheckSymbolExists)

libfind_pkg_check_modules(OpenH264_PKGCONF openh264)

find_path(OpenH264_INCLUDE_DIR
    NAMES wels/codec_api.h
    HINTS ${OpenH264_PKGCONF_INCLUDE_DIRS} ${OpenH264_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES OpenH264
)

find_library(OpenH264_LIBRARY
    NAMES libopenh264 openh264
    HINTS ${OpenH264_PKGCONF_LIBRARY_DIRS} ${OpenH264_PKGCONF_LIBDIR}
)

set(OpenH264_PROCESS_LIBS OpenH264_LIBRARY)
set(OpenH264_PROCESS_INCLUDES OpenH264_INCLUDE_DIR)
libfind_process(OpenH264)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenH264
    REQUIRED_VARS
        OpenH264_INCLUDE_DIR
        OpenH264_LIBRARIES
)
