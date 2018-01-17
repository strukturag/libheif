set (libheif_sources
bitstream.h
box.cc
box.h
error.cc
error.h
heif_api_structs.h
heif.cc
heif_context.cc
heif_context.h
heif_file.cc
heif_file.h
heif.h
heif_image.cc
heif_image.h
heif-version.h
logging.h
)

if(UNIX)
  include (${CMAKE_ROOT}/Modules/FindPkgConfig.cmake)
  pkg_check_modules (LIBDE265 libde265)
endif()

if(LIBDE265_FOUND)
  add_definitions(-DHAVE_LIBDE265=1)
  set (libheif_sources
    ${libheif_sources}
    heif_decoder_libde265.cc
    heif_decoder_libde265.h
  )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${LIBDE265_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LIBDE265_CFLAGS}")
endif()

# set(CMAKE_VERBOSE_MAKEFILE on)

add_definitions(-DLIBHEIF_EXPORTS)

add_library(${LIBHEIF_LIBRARY_NAME} SHARED ${libheif_sources})
if(LIBDE265_FOUND)
  target_link_libraries(${LIBHEIF_LIBRARY_NAME} ${LIBDE265_LIBRARIES})
endif()
if(CMAKE_COMPILER_IS_GNUCXX)
  set_target_properties(${LIBHEIF_LIBRARY_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  SET_TARGET_PROPERTIES(${LIBHEIF_LIBRARY_NAME} PROPERTIES COMPILE_FLAGS "-fPIC")
endif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
