include(LibFindMacros)
include(CheckStructHasMember)

libfind_pkg_check_modules(KVAZAAR_PKGCONF kvazaar)

find_path(KVAZAAR_INCLUDE_DIR
    NAMES kvazaar.h
    HINTS ${KVAZAAR_PKGCONF_INCLUDE_DIRS} ${KVAZAAR_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES KVAZAAR kvazaar
)

find_library(KVAZAAR_LIBRARY
    NAMES libkvazaar kvazaar kvazaar.dll
    HINTS ${KVAZAAR_PKGCONF_LIBRARY_DIRS} ${KVAZAAR_PKGCONF_LIBDIR}
)

set(KVAZAAR_PROCESS_LIBS KVAZAAR_LIBRARY)
set(KVAZAAR_PROCESS_INCLUDES KVAZAAR_INCLUDE_DIR)
libfind_process(KVAZAAR)

set(CMAKE_REQUIRED_INCLUDES ${KVAZAAR_INCLUDE_DIR})
CHECK_STRUCT_HAS_MEMBER("struct kvz_config" enable_logging_output kvazaar.h
                        HAVE_KVAZAAR_ENABLE_LOGGING LANGUAGE CXX)
unset(CMAKE_REQUIRED_INCLUDES)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(kvazaar
    REQUIRED_VARS
        KVAZAAR_INCLUDE_DIR
        KVAZAAR_LIBRARIES
)
