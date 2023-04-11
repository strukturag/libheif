include(CMakePackageConfigHelpers)

configure_file(heif_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

set(libheif_headers
    heif.h
    heif_cxx.h
    heif_plugin.h
    ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h plugins/heif_encoder_uncompressed.cc plugins/heif_encoder_uncompressed.h)

set(libheif_sources
    bitstream.cc
    box.cc
    error.cc
    heif.cc
    heif_context.cc
    heif_file.cc
    heif_image.cc
    heif_hevc.cc
    heif_avif.cc
    heif_colorconversion.cc
    heif_plugin_registry.cc
    heif_plugin.cc
    nclx.cc
    bitstream.h
    box.h
    error.h
    heif_api_structs.h
    heif_context.h
    heif_file.h
    heif_image.h
    heif_hevc.h
    heif_avif.h
    heif_colorconversion.h
    heif_plugin_registry.h
    heif_limits.h
    heif_init.cc heif_init.h
    nclx.h
    logging.h
    metadata_compression.cc
    metadata_compression.h
    common_utils.cc
    common_utils.h
    ${libheif_headers}
    )

add_library(heif ${libheif_sources})

if (ENABLE_PLUGIN_LOADING)
  if (WIN32)
    target_sources(heif PRIVATE plugins_windows.cc plugins_windows.h)
  else ()
    target_sources(heif PRIVATE plugins_unix.cc plugins_unix.h)
  endif ()
endif()

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

if(WITH_TESTS)
    # Same as the 'heif' target, but with all symbols exported.
    # Used by tests to test private APIs.
    # TODO: doesn't seem to work on windows?
    add_library(heif_internal ${libheif_sources})
    target_include_directories(heif_internal PRIVATE ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})
    target_include_directories(heif_internal
                                PUBLIC
                                    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}>
                                    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                    $<INSTALL_INTERFACE:include>)
    target_compile_definitions(heif_internal PUBLIC LIBHEIF_EXPORTS)
    set_target_properties(heif_internal PROPERTIES CXX_VISIBILITY_PRESET default)
endif()

if (PLUGIN_LOADING_SUPPORTED_AND_ENABLED)
    target_compile_definitions(heif PRIVATE ENABLE_PLUGIN_LOADING=1)
    target_link_libraries(heif PRIVATE ${CMAKE_DL_LIBS})
endif()

add_subdirectory(plugins)

if (WITH_DEFLATE_HEADER_COMPRESSION)
   find_package(ZLIB REQUIRED)
   if (ZLIB_FOUND)
     target_link_libraries(heif PRIVATE ${ZLIB_LIBRARY})
     target_include_directories(heif PRIVATE ${ZLIB_INCLUDE_DIRS})
     target_compile_definitions(heif PRIVATE WITH_DEFLATE_HEADER_COMPRESSION=1)
   endif ()
endif ()

if (ENABLE_MULTITHREADING_SUPPORT)
    target_compile_definitions(heif PRIVATE ENABLE_MULTITHREADING_SUPPORT=1)
    if (ENABLE_PARALLEL_TILE_DECODING)
        target_compile_definitions(heif PRIVATE ENABLE_PARALLEL_TILE_DECODING=1)
    endif()
endif()

if (ENABLE_UNCOMPRESSED_DECODER)
    target_compile_definitions(heif PRIVATE ENABLE_UNCOMPRESSED_DECODER=1)
    target_sources(heif PRIVATE
            uncompressed_image.h
            uncompressed_image.cc)
endif()

if (ENABLE_UNCOMPRESSED_ENCODER)
    target_compile_definitions(heif PRIVATE ENABLE_UNCOMPRESSED_ENCODER=1)
endif()

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
