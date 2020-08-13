include(LibFindMacros)

libfind_pkg_check_modules(AOM_PKGCONF aom)

find_path(AOM_INCLUDE_DIR
    NAMES aom/aom_decoder.h
    HINTS ${AOM_PKGCONF_INCLUDE_DIRS} ${AOM_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES AOM
)

find_library(AOM_LIBRARY
    NAMES libaom aom
    HINTS ${AOM_PKGCONF_LIBRARY_DIRS} ${AOM_PKGCONF_LIBDIR}
)

set(AOM_PROCESS_LIBS AOM_LIBRARY)
set(AOM_PROCESS_INCLUDES AOM_INCLUDE_DIR)
libfind_process(AOM)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AOM
    REQUIRED_VARS
        AOM_INCLUDE_DIR
        AOM_LIBRARIES
)
