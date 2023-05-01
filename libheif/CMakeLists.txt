include(CMakePackageConfigHelpers)

configure_file(heif_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

set(libheif_headers
        heif.h
        heif_cxx.h
        heif_plugin.h
        ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

set(libheif_sources
        bitstream.cc
        box.cc
        error.cc
        heif.cc
        context.cc
        file.cc
        pixelimage.cc
        hevc.cc
        avif.cc
        plugin_registry.cc
        heif_plugin.cc
        nclx.cc
        bitstream.h
        box.h
        error.h
        api_structs.h
        context.h
        file.h
        pixelimage.h
        hevc.h
        avif.h
        plugin_registry.h
        security_limits.h
        init.cc init.h
        nclx.h
        logging.h
        metadata_compression.cc
        metadata_compression.h
        common_utils.cc
        common_utils.h
        region.cc
        region.h
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
target_include_directories(heif PRIVATE ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})

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
    target_include_directories(heif PRIVATE ${LIBSHARPYUV_INCLUDE_DIR})
    target_link_libraries(heif PRIVATE ${LIBSHARPYUV_LIBRARIES} ${LIBSHARPYUV_LINKDIR})
else ()
    message("Not compiling 'libsharpyuv'")
endif ()

if (WITH_DEFLATE_HEADER_COMPRESSION)
    find_package(ZLIB REQUIRED)
    if (ZLIB_FOUND)
        target_link_libraries(heif PRIVATE ${ZLIB_LIBRARY})
        target_include_directories(heif PRIVATE ${ZLIB_INCLUDE_DIRS})
        target_compile_definitions(heif PRIVATE WITH_DEFLATE_HEADER_COMPRESSION=1)
    endif ()
endif ()

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
            uncompressed_image.h
            uncompressed_image.cc)
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
