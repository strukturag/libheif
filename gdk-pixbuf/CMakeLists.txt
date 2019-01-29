find_package(PkgConfig REQUIRED)
pkg_check_modules(GDKPIXBUF2 REQUIRED gdk-pixbuf-2.0)
execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} gdk-pixbuf-2.0 --variable gdk_pixbuf_moduledir OUTPUT_VARIABLE GDKPIXBUF2_MODULE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)

add_library(pixbufloader-heif MODULE pixbufloader-heif.c)

target_compile_definitions(pixbufloader-heif PRIVATE "-DGDK_PIXBUF_ENABLE_BACKEND")
target_include_directories(pixbufloader-heif PRIVATE ${GDKPIXBUF2_INCLUDE_DIRS})
target_link_libraries(pixbufloader-heif PUBLIC ${GDKPIXBUF2_LIBRARIES} ${LIBHEIF_LIBRARY_NAME})

install(TARGETS pixbufloader-heif LIBRARY DESTINATION ${GDKPIXBUF2_MODULE_DIR})
