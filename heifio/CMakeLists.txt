# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif/api ${libheif_SOURCE_DIR}/libheif)

add_library(heifio STATIC
        decoder.h
        decoder_y4m.cc
        decoder_y4m.h
        encoder.h
        encoder.cc
        encoder_y4m.h
        encoder_y4m.cc
        exif.h
        exif.cc
        stubs.cc)

target_link_libraries(heifio PRIVATE heif)

set_target_properties(heifio
        PROPERTIES
        VERSION ${PROJECT_VERSION})


target_compile_definitions(heifio
        PUBLIC
        LIBHEIF_EXPORTS
        HAVE_VISIBILITY)

find_package(TIFF)
if (TIFF_FOUND)
    target_sources(heifio PRIVATE decoder_tiff.cc decoder_tiff.h encoder_tiff.h encoder_tiff.cc)
    target_link_libraries(heifio PRIVATE TIFF::TIFF)
    target_compile_definitions(heifio PUBLIC HAVE_LIBTIFF=1)
endif()

find_package(JPEG)
if (TARGET JPEG::JPEG)
    target_compile_definitions(heifio PUBLIC -DHAVE_LIBJPEG=1)

    include(CheckCXXSourceCompiles)

    # this is needed for CheckCXXSourceCompiles
    set(CMAKE_REQUIRED_LIBRARIES ${JPEG_LIBRARIES})
    set(CMAKE_REQUIRED_INCLUDES ${JPEG_INCLUDE_DIRS})
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
    unset(CMAKE_REQUIRED_INCLUDES)

    if (HAVE_JPEG_WRITE_ICC_PROFILE)
        add_definitions(-DHAVE_JPEG_WRITE_ICC_PROFILE=1)
    endif ()

    target_link_libraries(heifio PRIVATE JPEG::JPEG)

    target_sources(heifio PRIVATE encoder_jpeg.cc encoder_jpeg.h)
    target_sources(heifio PRIVATE decoder.h decoder_jpeg.cc decoder_jpeg.h)
endif ()


find_package(PNG)
set(PNG_FOUND ${PNG_FOUND} PARENT_SCOPE)
if (TARGET PNG::PNG)
    target_compile_definitions(heifio PUBLIC -DHAVE_LIBPNG=1)

    target_link_libraries(heifio PRIVATE PNG::PNG)

    target_sources(heifio PRIVATE encoder_png.cc encoder_png.h)
    target_sources(heifio PRIVATE decoder_png.cc decoder_png.h)
endif ()

message("")
message("=== Active input formats for heif-enc ===")
if (JPEG_FOUND)
    message("JPEG: active")
else ()
    message("JPEG: ------ (libjpeg not found)")
endif ()
if (PNG_FOUND)
    message("PNG:  active")
else ()
    message("PNG:  ------ (libpng not found)")
endif ()
if (TIFF_FOUND)
    message("TIFF: active")
else ()
    message("TIFF: ------ (libtiff not found)")
endif ()
message("")

