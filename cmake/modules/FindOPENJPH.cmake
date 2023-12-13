include(LibFindMacros)
libfind_pkg_check_modules(OPENJPH_PKGCONF openjph)

find_path(OPENJPH_INCLUDE_DIR
    NAMES openjph/ojph_version.h
    HINTS ${OPENJPH_PKGCONF_INCLUDE_DIRS} ${OPENJPH_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES OPENJPH
)

find_library(OPENJPH_LIBRARY
    NAMES libopenjph openjph
    HINTS ${OPENJPH_PKGCONF_LIBRARY_DIRS} ${OPENJPH_PKGCONF_LIBDIR}
)

set(OPENJPH_PROCESS_LIBS OPENJPH_LIBRARY)
set(OPENJPH_PROCESS_INCLUDES OPENJPH_INCLUDE_DIR)
libfind_process(OPENJPH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OPENJPH
    REQUIRED_VARS
        OPENJPH_INCLUDE_DIR
        OPENJPH_LIBRARY
)
