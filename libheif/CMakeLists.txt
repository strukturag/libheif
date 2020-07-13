include(CMakePackageConfigHelpers)

configure_file(heif_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

set(libheif_headers
    bitstream.h
    box.h
    error.h
    heif_api_structs.h
    heif_context.h
    heif_file.h
    heif.h
    heif_image.h
    heif_hevc.h
    heif_avif.h
    heif_colorconversion.h
    heif_plugin_registry.h
    heif_limits.h
    heif_plugin.h
    nclx.h
    logging.h
    ${CMAKE_CURRENT_BINARY_DIR}/heif_version.h)

add_library(heif
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
    ${libheif_headers}
)

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

if(LIBDE265_FOUND)
    target_compile_definitions(heif PRIVATE HAVE_LIBDE265=1)
    target_sources(heif
                   PRIVATE
                       heif_decoder_libde265.cc
                       heif_decoder_libde265.h)

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LIBDE265_CFLAGS}")

    if (NOT "${LIBDE265_LIBRARY_DIRS}" STREQUAL "")
        set(LIBDE265_LINKDIR "-L${LIBDE265_LIBRARY_DIRS}")
    endif()

    target_link_libraries(heif PRIVATE ${LIBDE265_LIBRARIES} ${LIBDE265_LINKDIR})

    install(FILES heif_decoder_libde265.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
endif()

if(X265_FOUND)
    target_compile_definitions(heif PRIVATE HAVE_X265=1)
    target_sources(heif PRIVATE
        heif_encoder_x265.cc
        heif_encoder_x265.h
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${X265_CFLAGS}")

    if (NOT "${X265_LIBRARY_DIRS}" STREQUAL "")
        set(X265_LINKDIR "-L${X265_LIBRARY_DIRS}")
    endif()

    target_link_libraries(heif PRIVATE ${X265_LIBRARIES} ${X265_LINKDIR})

    install(FILES heif_encoder_x265.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
endif()

if(AOM_FOUND)
  target_compile_definitions(heif PRIVATE HAVE_AOM=1)
  target_sources(heif PRIVATE
    heif_decoder_aom.cc
    heif_decoder_aom.h
    heif_encoder_aom.cc
    heif_encoder_aom.h
  )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${AOM_CFLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${AOM_CFLAGS}")

  if (NOT "${AOM_LIBRARY_DIRS}" STREQUAL "")
    set(AOM_LINKDIR "-L${AOM_LIBRARY_DIRS}")
  endif()

  target_link_libraries(heif PRIVATE ${AOM_LIBRARIES} ${AOM_LINKDIR})
endif()

if(USE_LOCAL_RAV1E)
    target_compile_definitions(heif PRIVATE HAVE_RAV1E=1)
    target_sources(heif PRIVATE
            heif_encoder_rav1e.cc
            heif_encoder_rav1e.h
            )
    target_include_directories(heif PRIVATE ${CMAKE_SOURCE_DIR}/third-party/rav1e/target/release/include)
    target_link_libraries(heif PRIVATE ${CMAKE_SOURCE_DIR}/third-party/rav1e/target/release/librav1e.a)
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
