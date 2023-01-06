# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})

set (heif_convert_sources
  encoder.cc
  encoder.h
  encoder_y4m.cc
  encoder_y4m.h
  heif_convert.cc
  ../libheif/exif.cc
  ../libheif/exif.cc
)

set (additional_link_directories)
set (additional_libraries)
set (additional_includes)

find_package(JPEG)
find_package(PNG)

if(JPEG_FOUND)
add_definitions(-DHAVE_LIBJPEG=1)

include (CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_LIBRARIES JPEG::JPEG)
check_cxx_source_compiles("
#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>

int main() {
  jpeg_write_icc_profile(NULL, NULL, 0);
  return 0;
}
" HAVE_JPEG_WRITE_ICC_PROFILE)
unset(CMAKE_REQUIRED_LIBRARIES)
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
  JPEG::JPEG
)
endif()

if(PNG_FOUND)
  add_definitions(-DHAVE_LIBPNG=1)
  set (heif_convert_sources
      ${heif_convert_sources}
      encoder_png.cc
      encoder_png.h
            benchmark.h benchmark.cc)
  set (additional_libraries
    ${additional_libraries}
    PNG::PNG
  )
endif()

set (heif_info_sources
  heif_info.cc
)

set (heif_enc_sources
  heif_enc.cc
  ../libheif/exif.cc
  ../libheif/exif.cc
  benchmark.h
  benchmark.cc
)

set (heif_test_sources
  heif_test.cc
)

if(MSVC)
  set (getopt_sources
    ../extra/getopt.c
    ../extra/getopt.h
    ../extra/getopt_long.c
  )
  include_directories ("../extra")
endif()

add_executable (heif-convert ${heif_convert_sources} ${getopt_sources})
target_link_directories (heif-convert PRIVATE ${additional_link_directories})
target_link_libraries (heif-convert heif ${additional_libraries})
target_include_directories(heif-convert PRIVATE ${additional_includes})
install(TARGETS heif-convert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-convert.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

add_executable (heif-info ${heif_info_sources} ${getopt_sources})
target_link_libraries (heif-info heif)
install(TARGETS heif-info RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-info.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

add_executable (heif-enc ${heif_enc_sources} ${getopt_sources})
target_link_directories (heif-enc PRIVATE ${additional_link_directories})
target_link_libraries (heif-enc heif ${additional_libraries})
target_include_directories(heif-enc PRIVATE ${additional_includes})
install(TARGETS heif-enc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-enc.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

add_executable (heif-test ${heif_test_sources} ${getopt_sources})
target_link_libraries (heif-test heif)


if(PNG_FOUND)
  set (heif_thumbnailer_sources
    encoder.cc
    encoder.h
    heif_thumbnailer.cc
    encoder_png.cc
    encoder_png.h
    ../libheif/exif.h
    ../libheif/exif.cc
    )

  add_executable (heif-thumbnailer ${heif_thumbnailer_sources})
  target_link_libraries (heif-thumbnailer heif PNG::PNG)
  install(TARGETS heif-thumbnailer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES heif-thumbnailer.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()
