set (heif_convert_sources
  encoder.cc
  encoder.h
  heif_convert.cc
)

include (${CMAKE_ROOT}/Modules/FindJPEG.cmake)

if(JPEG_FOUND)
add_definitions(-DHAVE_LIBJPEG=1)
set (heif_convert_sources
  ${heif_convert_sources}
  encoder_jpeg.cc
  encoder_jpeg.h
)
endif()

if(UNIX)
  include (${CMAKE_ROOT}/Modules/FindPkgConfig.cmake)
  pkg_check_modules (LIBPNG libpng)
  if(LIBPNG_FOUND)
    add_definitions(-DHAVE_LIBPNG=1)
    set (heif_convert_sources
      ${heif_convert_sources}
      encoder_png.cc
      encoder_png.h
    )
  endif()
endif()

set (heif_info_sources
  heif_info.cc
)

set (heif_enc_sources
  heif_enc.cc
)

if(MSVC)
  set (heif_convert_sources
    ${heif_convert_sources}
    ../extra/getopt.c
    ../extra/getopt.h
    ../extra/getopt_long.c
  )
  set (heif_info_sources
    ${heif_info_sources}
    ../extra/getopt.c
    ../extra/getopt.h
    ../extra/getopt_long.c
  )
  set (heif_enc_sources
    ${heif_enc_sources}
    ../extra/getopt.c
    ../extra/getopt.h
    ../extra/getopt_long.c
  )
  include_directories ("../extra")
endif()

add_executable (heif-convert ${heif_convert_sources})
target_link_libraries (heif-convert ${LIBHEIF_LIBRARY_NAME} ${JPEG_LIBRARIES} ${LIBPNG_LIBRARIES})

add_executable (heif-info ${heif_info_sources})
target_link_libraries (heif-info ${LIBHEIF_LIBRARY_NAME})

add_executable (heif-enc ${heif_enc_sources})
target_link_libraries (heif-enc ${LIBHEIF_LIBRARY_NAME} ${JPEG_LIBRARIES} ${LIBPNG_LIBRARIES})
