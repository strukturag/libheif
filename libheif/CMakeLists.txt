include(CMakePackageConfigHelpers)

if(UNIX)
  include (${CMAKE_ROOT}/Modules/FindPkgConfig.cmake)
  pkg_check_modules (LIBDE265 libde265)
  pkg_check_modules (X265 x265)
endif()

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
    heif_colorconversion.h
    heif_plugin_registry.h
    heif_limits.h
    heif_plugin.h
    logging.h
    ${PROJECT_BINARY_DIR}/libheif/heif_version.h
)

add_library(${PROJECT_NAME} SHARED
    bitstream.cc
    box.cc
    error.cc
    heif.cc
    heif_context.cc
    heif_file.cc
    heif_image.cc
    heif_hevc.cc
    heif_colorconversion.cc
    heif_plugin_registry.cc
    heif_plugin.cc
    ${libheif_headers}
)

# Needed to find libheif/heif_version.h while compiling the library
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_BINARY_DIR})

target_compile_definitions(${PROJECT_NAME}
    PUBLIC 
        LIBHEIF_EXPORTS
        HAVE_VISIBILITY
)

if(LIBDE265_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_LIBDE265=1)
    target_sources(${PROJECT_NAME} PRIVATE
        heif_decoder_libde265.cc
        heif_decoder_libde265.h
    )

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LIBDE265_CFLAGS}")

    if (NOT "${LIBDE265_LIBRARY_DIRS}" STREQUAL "")
        set(LIBDE265_LINKDIR "-L${LIBDE265_LIBRARY_DIRS}")
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBDE265_LIBRARIES} ${LIBDE265_LINKDIR})

    install(FILES heif_decoder_libde265.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
endif()

if(X265_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_X265=1)
    target_sources(${PROJECT_NAME} PRIVATE
        heif_encoder_x265.cc
        heif_encoder_x265.h
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${X265_CFLAGS}")

    if (NOT "${X265_LIBRARY_DIRS}" STREQUAL "")
        set(X265_LINKDIR "-L${X265_LIBRARY_DIRS}")
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE ${X265_LIBRARIES} ${X265_LINKDIR})

    install(FILES heif_encoder_x265.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
endif()

write_basic_package_version_file(${PROJECT_NAME}ConfigVersion.cmake COMPATIBILITY ExactVersion)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${libheif_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

install(EXPORT ${PROJECT_NAME}Config DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
