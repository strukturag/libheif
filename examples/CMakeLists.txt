# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})

if (MSVC)
    set(getopt_sources
            ../extra/getopt.c
            ../extra/getopt.h
            ../extra/getopt_long.c
            )
    include_directories("../extra")
endif ()

add_executable(heif-info ${getopt_sources}
        heif_info.cc)
target_link_libraries(heif-info heif)
install(TARGETS heif-info RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-info.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)


add_executable(heif-convert ${getopt_sources}
        encoder.cc
        encoder.h
        encoder_y4m.cc
        encoder_y4m.h
        heif_convert.cc
        ../libheif/exif.cc
        ../libheif/exif.cc)
target_link_libraries(heif-convert PRIVATE heif)
install(TARGETS heif-convert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-convert.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)


add_executable(heif-enc ${getopt_sources}
        heif_enc.cc
        ../libheif/exif.cc
        ../libheif/exif.cc
        benchmark.h
        benchmark.cc
        encoder.cc
        decoder_y4m.cc
        decoder_y4m.h)
target_link_libraries(heif-enc PRIVATE heif)

install(TARGETS heif-enc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-enc.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)


add_executable(heif-test ${getopt_sources}
        heif_test.cc)
target_link_libraries(heif-test heif)


find_package(JPEG)
if (JPEG_FOUND)
    add_definitions(-DHAVE_LIBJPEG=1)

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

    target_link_libraries(heif-convert PRIVATE ${JPEG_LIBRARIES})
    target_link_libraries(heif-enc PRIVATE ${JPEG_LIBRARIES})
    target_include_directories(heif-convert PRIVATE ${JPEG_INCLUDE_DIRS})
    target_include_directories(heif-enc PRIVATE ${JPEG_INCLUDE_DIRS})

    target_sources(heif-convert PRIVATE encoder_jpeg.cc encoder_jpeg.h)
    target_sources(heif-enc PRIVATE decoder.h decoder_jpeg.cc decoder_jpeg.h)
endif ()


find_package(PNG)
if (PNG_FOUND)
    add_definitions(-DHAVE_LIBPNG=1)

    target_link_libraries(heif-convert PRIVATE ${PNG_LIBRARIES})
    target_link_libraries(heif-enc PRIVATE ${PNG_LIBRARIES})
    target_include_directories(heif-convert PRIVATE ${PNG_INCLUDE_DIR})
    target_include_directories(heif-enc PRIVATE ${PNG_INCLUDE_DIR})

    target_sources(heif-convert PRIVATE encoder_png.cc encoder_png.h)
    target_sources(heif-enc PRIVATE decoder_png.cc decoder_png.h)
endif ()


if (PNG_FOUND)
    add_executable(heif-thumbnailer ${getopt_sources}
            encoder.cc
            encoder.h
            heif_thumbnailer.cc
            encoder_png.cc
            encoder_png.h
            ../libheif/exif.h
            ../libheif/exif.cc)
    target_link_libraries(heif-thumbnailer heif ${PNG_LIBRARIES})
    target_include_directories(heif-thumbnailer PRIVATE ${PNG_INCLUDE_DIR})

    install(TARGETS heif-thumbnailer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES heif-thumbnailer.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif ()
