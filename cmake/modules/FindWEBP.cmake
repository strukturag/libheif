# Locates libwebp (decode + encode) and libwebpmux.
#
# Tries libwebp's upstream CMake config first (which defines the WebP::webp
# and WebP::libwebpmux imported targets). If that is not available, falls
# back to pkg-config / manual library search and defines the same imported
# targets so consumers can link uniformly.
#
# Sets:
#   WEBP_FOUND
#   WebP::webp        - imported target for libwebp
#   WebP::libwebpmux  - imported target for libwebpmux (depends on WebP::webp)

find_package(WebP QUIET CONFIG)

if(TARGET WebP::webp AND TARGET WebP::libwebpmux)
  set(WEBP_FOUND TRUE)
  if(NOT WEBP_FIND_QUIETLY)
    message(STATUS "Found WebP (CONFIG): ${WebP_DIR}")
  endif()
else()
  include(LibFindMacros)

  libfind_pkg_check_modules(WEBP_PKGCONF libwebp)
  libfind_pkg_check_modules(WEBPMUX_PKGCONF libwebpmux)

  find_path(WEBP_INCLUDE_DIR
      NAMES webp/decode.h webp/encode.h
      HINTS ${WEBP_PKGCONF_INCLUDE_DIRS} ${WEBP_PKGCONF_INCLUDEDIR}
  )

  find_path(WEBPMUX_INCLUDE_DIR
      NAMES webp/mux.h
      HINTS ${WEBPMUX_PKGCONF_INCLUDE_DIRS} ${WEBPMUX_PKGCONF_INCLUDEDIR} ${WEBP_INCLUDE_DIR}
  )

  find_library(WEBP_LIBRARY
      NAMES webp libwebp
      HINTS ${WEBP_PKGCONF_LIBRARY_DIRS} ${WEBP_PKGCONF_LIBDIR}
  )

  find_library(WEBPMUX_LIBRARY
      NAMES webpmux libwebpmux
      HINTS ${WEBPMUX_PKGCONF_LIBRARY_DIRS} ${WEBPMUX_PKGCONF_LIBDIR}
  )

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(WEBP
      REQUIRED_VARS
          WEBP_INCLUDE_DIR
          WEBP_LIBRARY
          WEBPMUX_INCLUDE_DIR
          WEBPMUX_LIBRARY
  )

  if(WEBP_FOUND)
    if(NOT TARGET WebP::webp)
      add_library(WebP::webp UNKNOWN IMPORTED)
      set_target_properties(WebP::webp PROPERTIES
          IMPORTED_LOCATION "${WEBP_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${WEBP_INCLUDE_DIR}"
      )
    endif()
    if(NOT TARGET WebP::libwebpmux)
      add_library(WebP::libwebpmux UNKNOWN IMPORTED)
      set_target_properties(WebP::libwebpmux PROPERTIES
          IMPORTED_LOCATION "${WEBPMUX_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${WEBPMUX_INCLUDE_DIR}"
          INTERFACE_LINK_LIBRARIES "WebP::webp"
      )
    endif()
  endif()

  mark_as_advanced(WEBP_INCLUDE_DIR WEBP_LIBRARY WEBPMUX_INCLUDE_DIR WEBPMUX_LIBRARY)
endif()
