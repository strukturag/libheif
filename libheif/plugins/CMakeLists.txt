
macro(plugin_compilation name varName optionName defineName)
    if (${varName}_FOUND)
        if (NOT "${${varName}_LIBRARY_DIRS}" STREQUAL "")
            set(${varName}_LINKDIR "-L${${varName}_LIBRARY_DIRS}")
        endif ()

        if (WITH_${optionName}_PLUGIN)
            add_library(heif-${name}
                    SHARED ${${optionName}_sources} ${${optionName}_extra_plugin_sources}
                    ../heif_plugin.cc
                    )
            set_target_properties(heif-${name}
                    PROPERTIES
                    VERSION ${PROJECT_VERSION}
                    SOVERSION ${PROJECT_VERSION_MAJOR})
            target_compile_definitions(heif-${name}
                    PUBLIC
                    LIBHEIF_EXPORTS
                    HAVE_VISIBILITY)
            target_compile_definitions(heif-${name} PRIVATE PLUGIN_${defineName}=1)
            target_include_directories(heif-${name} PRIVATE ${PROJECT_SOURCE_DIR} ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR} ${${varName}_INCLUDE_DIR})
            target_link_libraries(heif-${name} PRIVATE ${${varName}_LIBRARIES} ${${varName}_LINKDIR})

            install(TARGETS heif-${name}
                    LIBRARY DESTINATION ${PLUGIN_DIRECTORY}
                    )
        else ()
            target_sources(heif PRIVATE ${${optionName}_sources})
            target_compile_definitions(heif PRIVATE HAVE_${defineName}=1)
            target_include_directories(heif PRIVATE ${${varName}_INCLUDE_DIR})
            target_link_libraries(heif PRIVATE ${${varName}_LIBRARIES} ${${varName}_LINKDIR})
        endif ()

        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${${varName}_CFLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${${varName}_CFLAGS}")
    endif ()
endmacro()


set(X265_sources heif_encoder_x265.h heif_encoder_x265.cc)
set(X265_extra_plugin_sources)
plugin_compilation(x265 X265 X265 X265)

set(LIBDE265_sources heif_decoder_libde265.cc heif_decoder_libde265.h)
set(LIBDE265_extra_plugin_sources ../heif_image.cc ../error.cc)
plugin_compilation(libde265 LIBDE265 LIBDE265 LIBDE265)

set(DAV1D_sources heif_decoder_dav1d.cc heif_decoder_dav1d.h)
set(DAV1D_extra_plugin_sources)
plugin_compilation(dav1d DAV1D DAV1D DAV1D)

set(AOM_DECODER_sources heif_decoder_aom.cc heif_decoder_aom.h)
set(AOM_DECODER_extra_plugin_sources)
plugin_compilation(aomdec AOM AOM_DECODER AOM_DECODER)

set(AOM_ENCODER_sources heif_encoder_aom.cc heif_encoder_aom.h)
set(AOM_ENCODER_extra_plugin_sources ../heif_avif.cc ../error.cc)
plugin_compilation(aomenc AOM AOM_ENCODER AOM_ENCODER)

set(SvtEnc_sources heif_encoder_svt.cc heif_encoder_svt.h)
set(SvtEnc_extra_plugin_sources)
plugin_compilation(svtenc SvtEnc SvtEnc SvtEnc)

set(RAV1E_sources heif_encoder_rav1e.cc heif_encoder_rav1e.h)
set(RAV1E_extra_plugin_sources ../box.cc ../error.cc)
plugin_compilation(rav1e RAV1E RAV1E RAV1E)
