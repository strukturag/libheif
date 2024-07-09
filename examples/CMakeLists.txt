# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif/api ${libheif_SOURCE_DIR}/libheif)

if (MSVC)
    set(getopt_sources
            ../extra/getopt.c
            ../extra/getopt.h
            ../extra/getopt_long.c
            )
    include_directories("../extra")
endif ()

add_executable(heif-info ${getopt_sources}
        heif_info.cc
        common.cc
        common.h)
target_link_libraries(heif-info heif)
install(TARGETS heif-info RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-info.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)


add_executable(heif-dec ${getopt_sources}
        encoder.cc
        encoder.h
        encoder_y4m.cc
        encoder_y4m.h
        heif_dec.cc
        ../libheif/exif.cc
        common.cc
        common.h)
target_link_libraries(heif-dec PRIVATE heif)
install(TARGETS heif-dec RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-dec.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# create symbolic link from the old name `heif-convert` to `heif-dec`
if(NOT WIN32)
  install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/heif-dec${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/heif-convert${CMAKE_EXECUTABLE_SUFFIX})")
else()
  install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/heif-dec${CMAKE_EXECUTABLE_SUFFIX} ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/heif-convert${CMAKE_EXECUTABLE_SUFFIX})")
endif()


add_executable(heif-enc ${getopt_sources}
        heif_enc.cc
        ../libheif/exif.cc
        ../libheif/exif.cc
        benchmark.h
        benchmark.cc
        encoder.cc
        decoder_y4m.cc
        decoder_y4m.h
        common.cc
        common.h)
target_link_libraries(heif-enc PRIVATE heif)
install(TARGETS heif-enc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES heif-enc.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

if (WITH_HEADER_COMPRESSION)
    target_compile_definitions(heif-enc PRIVATE WITH_HEADER_COMPRESSION=1)
endif ()


add_executable(heif-test ${getopt_sources}
        heif_test.cc
        common.cc
        common.h)
target_link_libraries(heif-test heif)


find_package(JPEG)
if (TARGET JPEG::JPEG)
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

    target_link_libraries(heif-dec PRIVATE JPEG::JPEG)
    target_link_libraries(heif-enc PRIVATE JPEG::JPEG)

    target_sources(heif-dec PRIVATE encoder_jpeg.cc encoder_jpeg.h)
    target_sources(heif-enc PRIVATE decoder.h decoder_jpeg.cc decoder_jpeg.h)
endif ()


find_package(PNG)
if (TARGET PNG::PNG)
    add_definitions(-DHAVE_LIBPNG=1)

    target_link_libraries(heif-dec PRIVATE PNG::PNG)
    target_link_libraries(heif-enc PRIVATE PNG::PNG)

    target_sources(heif-dec PRIVATE encoder_png.cc encoder_png.h)
    target_sources(heif-enc PRIVATE decoder_png.cc decoder_png.h)

    add_executable(heif-thumbnailer ${getopt_sources}
            encoder.cc
            encoder.h
            heif_thumbnailer.cc
            encoder_png.cc
            encoder_png.h
            ../libheif/exif.h
            ../libheif/exif.cc
            common.cc
            common.h)
    target_link_libraries(heif-thumbnailer heif PNG::PNG)

    install(TARGETS heif-thumbnailer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES heif-thumbnailer.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif ()

find_package(TIFF)
if (TIFF_FOUND)
    target_link_libraries(heif-enc PRIVATE TIFF::TIFF)
    target_sources(heif-enc PRIVATE decoder_tiff.cc decoder_tiff.h)
    target_compile_definitions(heif-enc PUBLIC HAVE_LIBTIFF=1)
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

