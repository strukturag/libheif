

if (SvtEnc_FOUND)
    if (WITH_SvtEnc_BUILTIN)
        target_sources(heif PRIVATE
                heif_encoder_svt.cc
                heif_encoder_svt.h
                )
        target_compile_definitions(heif PRIVATE HAVE_SvtEnc=1)
        target_link_libraries(heif PRIVATE ${SvtEnc_LIBRARIES} ${SvtEnc_LINKDIR})
    else()
        add_library(heif-svtenc
                SHARED
                heif_encoder_svt.cc
                heif_encoder_svt.h
                ../heif_plugin.cc
                )
        set_target_properties(heif-svtenc
                PROPERTIES
                VERSION ${PROJECT_VERSION}
                SOVERSION ${PROJECT_VERSION_MAJOR})
        target_compile_definitions(heif-svtenc
                PUBLIC
                LIBHEIF_EXPORTS
                HAVE_VISIBILITY)
        target_compile_definitions(heif-svtenc PRIVATE PLUGIN_SVTENC=1)
        target_include_directories(heif-svtenc PRIVATE ${PROJECT_SOURCE_DIR} ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})
        target_link_libraries(heif-svtenc PRIVATE ${SvtEnc_LIBRARIES} ${SvtEnc_LINKDIR})

        install(TARGETS heif-svtenc
                LIBRARY DESTINATION ${PLUGIN_DIRECTORY}
                )
    endif ()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SvtEnc_CFLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SvtEnc_CFLAGS}")

    if (NOT "${SvtEnc_LIBRARY_DIRS}" STREQUAL "")
        set(SvtEnc_LINKDIR "-L${SvtEnc_LIBRARY_DIRS}")
    endif ()

    include_directories(SYSTEM ${SvtEnc_INCLUDE_DIR})
endif ()



if (X265_FOUND)
    if (WITH_X265_BUILTIN)
        target_sources(heif PRIVATE
                heif_encoder_x265.cc
                heif_encoder_x265.h
                )
        target_compile_definitions(heif PRIVATE HAVE_X265=1)
        target_link_libraries(heif PRIVATE ${X265_LIBRARIES} ${X265_LINKDIR})
    else()
        add_library(heif-x265
                SHARED
                heif_encoder_x265.cc
                heif_encoder_x265.h
                ../heif_plugin.cc
                )
        set_target_properties(heif-x265
                PROPERTIES
                VERSION ${PROJECT_VERSION}
                SOVERSION ${PROJECT_VERSION_MAJOR})
        target_compile_definitions(heif-x265
                PUBLIC
                LIBHEIF_EXPORTS
                HAVE_VISIBILITY)
        target_compile_definitions(heif-x265 PRIVATE PLUGIN_X265=1)
        target_include_directories(heif-x265 PRIVATE ${PROJECT_SOURCE_DIR} ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})
        target_link_libraries(heif-x265 PRIVATE ${X265_LIBRARIES} ${X265_LINKDIR})

        install(TARGETS heif-x265
                LIBRARY DESTINATION ${PLUGIN_DIRECTORY}
                )
    endif ()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${X265_CFLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${X265_CFLAGS}")

    if (NOT "${X265_LIBRARY_DIRS}" STREQUAL "")
        set(X265_LINKDIR "-L${X265_LIBRARY_DIRS}")
    endif ()

    include_directories(SYSTEM ${X265_INCLUDE_DIR})
endif ()
