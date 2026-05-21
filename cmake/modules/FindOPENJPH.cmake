find_package(OPENJPH QUIET CONFIG NAMES openjph)

if(TARGET openjph)
  if(NOT OPENJPH_FIND_QUIETLY)
    message(STATUS "Found openjph: ${OPENJPH_DIR}")
  endif()
  set(OPENJPH_LIBRARIES "openjph")

  # OpenJPH's exported config sets the include directory to
  # <prefix>/include/openjph, but libheif's sources include the headers with
  # the "openjph/" prefix (e.g. #include "openjph/ojph_mem.h"). Add the parent
  # directory so those includes resolve with both header layouts.
  get_target_property(_openjph_incdirs openjph INTERFACE_INCLUDE_DIRECTORIES)
  if(_openjph_incdirs)
    foreach(_dir IN LISTS _openjph_incdirs)
      list(APPEND OPENJPH_INCLUDE_DIRS "${_dir}")
      if(_dir MATCHES "/openjph$")
        get_filename_component(_parent "${_dir}" DIRECTORY)
        list(APPEND OPENJPH_INCLUDE_DIRS "${_parent}")
      endif()
    endforeach()
    unset(_parent)
  endif()
  unset(_openjph_incdirs)
else()
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
endif()
