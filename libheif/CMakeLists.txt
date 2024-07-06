include(CMakePackageConfigHelpers)

configure_file(api/libheif/heif_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

set(libheif_headers
        api/libheif/heif.h
        api/libheif/heif_cxx.h
        api/libheif/heif_plugin.h
        api/libheif/heif_properties.h
        api/libheif/heif_regions.h
        api/libheif/heif_items.h
        ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

set(libheif_sources
        bitstream.cc
        bitstream.h
        box.cc
        box.h
        error.cc
        error.h
        context.cc
        context.h
        file.cc
        file.h
        pixelimage.cc
        pixelimage.h
        plugin_registry.cc
        nclx.cc
        nclx.h
        plugin_registry.h
        security_limits.h
        init.cc
        init.h
        logging.h
        logging.cc
        compression.h
        compression_brotli.cc
        compression_zlib.cc
        common_utils.cc
        common_utils.h
        region.cc
        region.h
        api/libheif/api_structs.h
        api/libheif/heif.cc
        api/libheif/heif_regions.cc
        api/libheif/heif_plugin.cc
        api/libheif/heif_properties.cc
        api/libheif/heif_items.cc
        api/libheif/heif_experimental.h
        api/libheif/heif_experimental.cc
        codecs/hevc.cc
        codecs/hevc.h
        codecs/avif.cc
        codecs/avif.h
        codecs/jpeg.h
        codecs/jpeg.cc
        codecs/jpeg2000.h
        codecs/jpeg2000.cc
        codecs/vvc.h
        codecs/vvc.cc
        codecs/avc.h
        codecs/avc.cc
        codecs/mask_image.cc
        codecs/mask_image.h
        color-conversion/colorconversion.cc
        color-conversion/colorconversion.h
        color-conversion/rgb2yuv.cc
        color-conversion/rgb2yuv.h
        color-conversion/rgb2yuv_sharp.cc
        color-conversion/rgb2yuv_sharp.h
        color-conversion/yuv2rgb.cc
        color-conversion/yuv2rgb.h
        color-conversion/rgb2rgb.cc
        color-conversion/rgb2rgb.h
        color-conversion/monochrome.cc
        color-conversion/monochrome.h
        color-conversion/hdr_sdr.cc
        color-conversion/hdr_sdr.h
        color-conversion/alpha.cc
        color-conversion/alpha.h
        color-conversion/chroma_sampling.cc
        color-conversion/chroma_sampling.h
        ${libheif_headers})

add_library(heif ${libheif_sources})

if (ENABLE_PLUGIN_LOADING)
    if (WIN32)
        target_sources(heif PRIVATE plugins_windows.cc plugins_windows.h)
    else ()
        target_sources(heif PRIVATE plugins_unix.cc plugins_unix.h)
    endif ()
endif ()

# Needed to find libheif/heif_version.h while compiling the library
target_include_directories(heif PRIVATE ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif ${libheif_SOURCE_DIR}/libheif/api)

# Propagate include/libheif to consume the headers from other projects
target_include_directories(heif
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

set_target_properties(heif
        PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

if (APPLE)
    set_target_properties(heif
            PROPERTIES
	    LINK_FLAGS "-Wl,-compatibility_version,${MACOS_COMPATIBLE_VERSION}")
endif ()

target_compile_definitions(heif
        PUBLIC
        LIBHEIF_EXPORTS
        HAVE_VISIBILITY)

if (PLUGIN_LOADING_SUPPORTED_AND_ENABLED)
    target_compile_definitions(heif PRIVATE ENABLE_PLUGIN_LOADING=1)
    target_link_libraries(heif PRIVATE ${CMAKE_DL_LIBS})
endif ()

add_subdirectory(plugins)

if (LIBSHARPYUV_FOUND)
    message("Compiling in 'libsharpyuv'")
    target_compile_definitions(heif PUBLIC HAVE_LIBSHARPYUV=1)
    target_include_directories(heif PRIVATE ${LIBSHARPYUV_INCLUDE_DIRS})
    target_link_libraries(heif PRIVATE ${LIBSHARPYUV_LIBRARIES})
else ()
    message("Not compiling 'libsharpyuv'")
endif ()

if (WITH_DEFLATE_HEADER_COMPRESSION OR WITH_UNCOMPRESSED_CODEC)
    find_package(ZLIB REQUIRED)
    target_link_libraries(heif PRIVATE ZLIB::ZLIB)
    target_compile_definitions(heif PRIVATE WITH_ZLIB_COMPRESSION=1)
endif ()

if (WITH_DEFLATE_HEADER_COMPRESSION)
    target_compile_definitions(heif PRIVATE WITH_DEFLATE_HEADER_COMPRESSION=1)
endif ()

if (HAVE_BROTLI)
    target_compile_definitions(heif PUBLIC HAVE_BROTLI=1)
    target_include_directories(heif PRIVATE ${BROTLI_INCLUDE_DIRS})
    target_link_libraries(heif PRIVATE ${BROTLI_LIBS})
endif()

if (ENABLE_MULTITHREADING_SUPPORT)
    find_package(Threads)
    target_link_libraries(heif PRIVATE ${CMAKE_THREAD_LIBS_INIT})

    target_compile_definitions(heif PRIVATE ENABLE_MULTITHREADING_SUPPORT=1)
    if (ENABLE_PARALLEL_TILE_DECODING)
        target_compile_definitions(heif PRIVATE ENABLE_PARALLEL_TILE_DECODING=1)
    endif ()
endif ()

if (WITH_UNCOMPRESSED_CODEC)
    target_compile_definitions(heif PUBLIC WITH_UNCOMPRESSED_CODEC=1)
    target_sources(heif PRIVATE
            codecs/uncompressed_box.h
            codecs/uncompressed_box.cc
            codecs/uncompressed_image.h
            codecs/uncompressed_image.cc)
endif ()

write_basic_package_version_file(${PROJECT_NAME}-config-version.cmake COMPATIBILITY ExactVersion)

install(TARGETS heif EXPORT ${PROJECT_NAME}-config
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )

install(FILES ${libheif_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

install(EXPORT ${PROJECT_NAME}-config DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")


# --- on Windows, copy the DLL into the executable directory for easier development

if (WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(TARGET heif POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:heif>
            $<TARGET_FILE_DIR:heif>/../examples
    )
endif ()
