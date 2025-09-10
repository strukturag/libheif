find_package(AOM QUIET CONFIG)

if(TARGET AOM::aom)
  if(NOT AOM_FIND_QUIETLY)
    message(STATUS "Found AOM: ${AOM_DIR}")
  endif()
else()
  include(LibFindMacros)

  libfind_pkg_check_modules(AOM_PKGCONF aom)

  find_path(AOM_INCLUDE_DIR
      NAMES aom/aom_decoder.h aom/aom_encoder.h
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
          AOM_LIBRARY
  )
endif()

if(AOM_FOUND)
  include(CheckSymbolExists)

  list(APPEND CMAKE_REQUIRED_INCLUDES ${AOM_INCLUDE_DIRS})
  check_symbol_exists(AOM_USAGE_GOOD_QUALITY aom/aom_encoder.h aom_usage_flag_exists)
  unset(CMAKE_REQUIRED_INCLUDES)

  if(EXISTS "${AOM_INCLUDE_DIRS}/aom/aom_decoder.h")
    set(AOM_DECODER_FOUND YES)
  else()
    set(AOM_DECODER_FOUND NO)
  endif()

  if((EXISTS "${AOM_INCLUDE_DIRS}/aom/aom_encoder.h") AND "${aom_usage_flag_exists}")
    set(AOM_ENCODER_FOUND YES)
  else()
    set(AOM_ENCODER_FOUND NO)
  endif()
endif()
