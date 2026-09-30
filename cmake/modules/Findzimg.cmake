include(LibFindMacros)
libfind_pkg_check_modules(ZIMG_PKGCONF zimg)

find_path(ZIMG_INCLUDE_DIR
    NAMES zimg.h
    HINTS ${ZIMG_PKGCONF_INCLUDE_DIRS} ${ZIMG_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES ZIMG
)
find_library(ZIMG_LIBRARY
    NAMES zimg
    HINTS ${ZIMG_PKGCONF_LIBRARY_DIRS} ${ZIMG_PKGCONF_LIBDIR}
)
set(ZIMG_PROCESS_LIBS ${ZIMG_LIBRARY})
set(ZIMG_PROCESS_INCLUDES ${ZIMG_INCLUDE_DIR})
libfind_process(ZIMG)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZIMG
    REQUIRED_VARS
        ZIMG_INCLUDE_DIRS
        ZIMG_LIBRARIES
)
