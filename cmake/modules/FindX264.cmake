include(LibFindMacros)
libfind_pkg_check_modules(X264_PKGCONF x264)

find_path(X264_INCLUDE_DIR
    NAMES x264.h
    HINTS ${X264_PKGCONF_INCLUDE_DIRS} ${X264_PKGCONF_INCLUDEDIR}
    PATH_SUFFIXES X264
)

find_library(X264_LIBRARY
    NAMES libx264 x264
    HINTS ${X264_PKGCONF_LIBRARY_DIRS} ${X264_PKGCONF_LIBDIR}
)

set(X264_PROCESS_LIBS X264_LIBRARY)
set(X264_PROCESS_INCLUDES X264_INCLUDE_DIR)
libfind_process(X264)

if(X264_INCLUDE_DIR)
  set(x264_config_file "${X264_INCLUDE_DIR}/x264_config.h")
  if(EXISTS ${x264_config_file})
      file(STRINGS
           ${x264_config_file}
           TMP
           REGEX "#define X264_BUILD .*$")
      string(REGEX REPLACE "#define X264_BUILD" "" TMP ${TMP})
      string(REGEX MATCHALL "[0-9.]+" X264_BUILD ${TMP})
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(X264
    REQUIRED_VARS
        X264_INCLUDE_DIR
        X264_LIBRARIES
    VERSION_VAR
        X264_BUILD
)
