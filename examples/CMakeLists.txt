set (heif_convert_sources
  encoder.cc
  encoder.h
  encoder_y4m.cc
  encoder_y4m.h
  heif_convert.cc
)

set (additional_libraries)

include (${CMAKE_ROOT}/Modules/FindJPEG.cmake)

if(JPEG_FOUND)
add_definitions(-DHAVE_LIBJPEG=1)

include (${CMAKE_ROOT}/Modules/CheckCXXSourceCompiles.cmake)

set(CMAKE_REQUIRED_LIBRARIES ${JPEG_LIBRARIES})
check_cxx_source_compiles("
#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>

int main() {
  jpeg_write_icc_profile(NULL, NULL, 0);
  return 0;
}
" HAVE_JPEG_WRITE_ICC_PROFILE)
if(HAVE_JPEG_WRITE_ICC_PROFILE)
  add_definitions(-DHAVE_JPEG_WRITE_ICC_PROFILE=1)
endif()

set (heif_convert_sources
  ${heif_convert_sources}
  encoder_jpeg.cc
  encoder_jpeg.h
)
set (additional_libraries
  ${additional_libraries}
  ${JPEG_LIBRARIES}
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
    set (additional_libraries
      ${additional_libraries}
      ${LIBPNG_LIBRARIES}
    )
  endif()
endif()

set (heif_info_sources
  heif_info.cc
)

set (heif_enc_sources
  heif_enc.cc
)

set (heif_test_sources
  heif_test.cc
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
  set (heif_test_sources
    ${heif_test_sources}
    ../extra/getopt.c
    ../extra/getopt.h
    ../extra/getopt_long.c
  )
  include_directories ("../extra")
endif()

add_executable (heif-convert ${heif_convert_sources})
target_link_libraries (heif-convert ${PROJECT_NAME} ${additional_libraries})

add_executable (heif-info ${heif_info_sources})
target_link_libraries (heif-info ${PROJECT_NAME})

add_executable (heif-enc ${heif_enc_sources})
target_link_libraries (heif-enc ${PROJECT_NAME} ${additional_libraries})

add_executable (heif-test ${heif_test_sources})
target_link_libraries (heif-test ${PROJECT_NAME})


if(LIBPNG_FOUND)
  set (heif_thumbnailer_sources
    encoder.cc
    encoder.h
    heif_thumbnailer.cc
    encoder_png.cc
    encoder_png.h
    )

  add_executable (heif-thumbnailer ${heif_thumbnailer_sources})
  target_link_libraries (heif-thumbnailer ${PROJECT_NAME} ${LIBPNG_LIBRARIES})
endif()
