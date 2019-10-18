include(CMakePackageConfigHelpers)

set (libheif_sources
bitstream.cc
box.cc
error.cc
heif.cc
heif_context.cc
heif_file.cc
heif_image.cc
heif_hevc.cc
heif_colorconversion.cc
heif_plugin_registry.cc
heif_plugin.cc
)

set (libheif_headers
bitstream.h
box.h
error.h
heif_api_structs.h
heif_context.h
heif_file.h
heif.h
heif_image.h
heif_hevc.h
heif_colorconversion.h
heif_plugin_registry.h
heif_limits.h
heif_plugin.h
heif_version.h
logging.h
)

if(UNIX)
  include (${CMAKE_ROOT}/Modules/FindPkgConfig.cmake)
  pkg_check_modules (LIBDE265 libde265)
  pkg_check_modules (X265 x265)
endif()

if(LIBDE265_FOUND)
  add_definitions(-DHAVE_LIBDE265=1)
  set (libheif_sources
    ${libheif_sources}
    heif_decoder_libde265.cc
  )
  set (libheif_headers
    ${libheif_headers}
    heif_decoder_libde265.h
  )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${LIBDE265_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LIBDE265_CFLAGS}")

  if (NOT "${LIBDE265_LIBRARY_DIRS}" STREQUAL "")
    set(LIBDE265_LINKDIR "-L${LIBDE265_LIBRARY_DIRS}")
  endif()
endif()

if(X265_FOUND)
  add_definitions(-DHAVE_X265=1)
  set (libheif_sources
    ${libheif_sources}
    heif_encoder_x265.cc
  )
  set (libheif_headers
    ${libheif_headers}
    heif_encoder_x265.h
  )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${X265_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${X265_CFLAGS}")

  if (NOT "${X265_LIBRARY_DIRS}" STREQUAL "")
    set(X265_LINKDIR "-L${X265_LIBRARY_DIRS}")
  endif()
endif()

# set(CMAKE_VERBOSE_MAKEFILE on)

add_definitions(-DHAVE_VISIBILITY)
add_definitions(-DLIBHEIF_EXPORTS)

add_library(${PROJECT_NAME} SHARED ${libheif_sources} ${libheif_headers})

if(LIBDE265_FOUND)
    target_link_libraries(${PROJECT_NAME} ${LIBDE265_LIBRARIES} ${LIBDE265_LINKDIR})
endif()

if(X265_FOUND)
    target_link_libraries(${PROJECT_NAME} ${X265_LIBRARIES} ${X265_LINKDIR})
endif()

