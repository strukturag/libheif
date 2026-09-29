
macro(plugin_compilation name varName foundName optionName defineName)
    if (${foundName} AND WITH_${optionName})
        if (PLUGIN_LOADING_SUPPORTED_AND_ENABLED AND WITH_${optionName}_PLUGIN)
            message("Compiling '" ${name} "' as dynamic plugin")
            add_library(heif-${name}
                    MODULE ${${optionName}_sources} ${${optionName}_extra_plugin_sources}
                    ../api/libheif/heif_plugin.cc
                    )
            if (NOT APPLE)
                set_target_properties(heif-${name}
                        PROPERTIES
                        VERSION ${PROJECT_VERSION}
                        SOVERSION ${PROJECT_VERSION_MAJOR})
            endif ()
            target_compile_definitions(heif-${name}
                    PUBLIC
                    LIBHEIF_EXPORTS
                    HAVE_VISIBILITY)
            target_compile_definitions(heif-${name} PRIVATE PLUGIN_${defineName}=1)
            target_include_directories(heif-${name} PRIVATE ${PROJECT_SOURCE_DIR} ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif ${libheif_SOURCE_DIR}/libheif/api ${${varName}_INCLUDE_DIRS})
            target_link_libraries(heif-${name} PRIVATE ${${varName}_LIBRARIES} heif)

            install(TARGETS heif-${name}
                    LIBRARY DESTINATION ${COMPUTED_PLUGIN_INSTALL_DIRECTORY}
                    )
        else ()
            message("Compiling '" ${name} "' as built-in backend")
            target_sources(heif PRIVATE ${${optionName}_sources})
            target_compile_definitions(heif PRIVATE HAVE_${defineName}=1)
            target_include_directories(heif PRIVATE ${${varName}_INCLUDE_DIRS})
            target_link_directories(heif PRIVATE ${${varName}_LINK_DIRS})
            if (${varName}_DEBUG_LIBRARY)
                target_link_libraries(heif PRIVATE debug ${${varName}_DEBUG_LIBRARY})
                target_link_libraries(heif PRIVATE optimized ${${varName}_LIBRARIES})
            else()
                target_link_libraries(heif PRIVATE ${${varName}_LIBRARIES})
            endif()
        endif ()

        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${${varName}_CFLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${${varName}_CFLAGS}")
    else()
        message("Not compiling '" ${name} "' backend")
    endif ()
endmacro()

set(X265_sources encoder_x265.h encoder_x265.cc)
set(X265_extra_plugin_sources)
plugin_compilation(x265 X265 X265_FOUND X265 X265)

set(LIBDE265_sources decoder_libde265.cc decoder_libde265.h)
set(LIBDE265_extra_plugin_sources ../error.cc)
plugin_compilation(libde265 LIBDE265 LIBDE265_FOUND LIBDE265 LIBDE265)

set(DAV1D_sources decoder_dav1d.cc decoder_dav1d.h)
set(DAV1D_extra_plugin_sources ../error.cc ../common_utils.cc ../common_utils.h)
plugin_compilation(dav1d DAV1D DAV1D_FOUND DAV1D DAV1D)

set(AOM_DECODER_sources decoder_aom.cc decoder_aom.h)
set(AOM_DECODER_extra_plugin_sources)
plugin_compilation(aomdec AOM AOM_DECODER_FOUND AOM_DECODER AOM_DECODER)

set(AOM_ENCODER_sources encoder_aom.cc encoder_aom.h)
set(AOM_ENCODER_extra_plugin_sources ../error.cc ../common_utils.cc ../common_utils.h)
plugin_compilation(aomenc AOM AOM_ENCODER_FOUND AOM_ENCODER AOM_ENCODER)

set(SvtEnc_sources encoder_svt.cc encoder_svt.h)
set(SvtEnc_extra_plugin_sources ../error.cc ../common_utils.cc ../common_utils.h)
plugin_compilation(svtenc SvtEnc SvtEnc_FOUND SvtEnc SvtEnc)

set(RAV1E_sources encoder_rav1e.cc encoder_rav1e.h)
set(RAV1E_extra_plugin_sources ../error.cc)
plugin_compilation(rav1e RAV1E RAV1E_FOUND RAV1E RAV1E)

set(JPEG_DECODER_sources decoder_jpeg.cc decoder_jpeg.h)
set(JPEG_DECODER_extra_plugin_sources)
plugin_compilation(jpegdec JPEG JPEG_FOUND JPEG_DECODER JPEG_DECODER)

set(JPEG_ENCODER_sources encoder_jpeg.cc encoder_jpeg.h)
set(JPEG_ENCODER_extra_plugin_sources)
plugin_compilation(jpegenc JPEG JPEG_FOUND JPEG_ENCODER JPEG_ENCODER)

set(OpenJPEG_DECODER_sources decoder_openjpeg.cc decoder_openjpeg.h)
set(OpenJPEG_DECODER_extra_plugin_sources ../error.cc ../common_utils.cc ../common_utils.h)
plugin_compilation(j2kdec OPENJPEG OPENJPEG_FOUND OpenJPEG_DECODER OPENJPEG_DECODER)

set(OpenJPEG_ENCODER_sources encoder_openjpeg.cc encoder_openjpeg.h)
set(OpenJPEG_ENCODER_extra_plugin_sources)
plugin_compilation(j2kenc OPENJPEG OPENJPEG_FOUND OpenJPEG_ENCODER OPENJPEG_ENCODER)

set(KVAZAAR_sources encoder_kvazaar.cc encoder_kvazaar.h)
set(KVAZAAR_extra_plugin_sources)
plugin_compilation(kvazaar KVAZAAR KVAZAAR_FOUND KVAZAAR KVAZAAR)

# --- HM, the HEVC reference software
#
# HM is no library: a process can only contain one version of it, it prints to stdout and it
# exits on errors. We compile the HM sources here instead of linking to a build of HM, because
# that allows us to put every version of HM ("variant") into a C++ namespace of its own
# without changing the HM sources: each source file is compiled through a generated file that
# includes it inside of the namespace. See hm_as_library.h.
#
#   hm_variant(<variant> <function>)
#
#   variant   name of the variant, as in HM_<variant>_SOURCES
#   function  the function that returns the variant, see encoder_hm_variant.h
#
# The object files are appended to HM_sources.

macro(hm_variant variant function)
    string(TOLOWER "${variant}" hm_variant_name)
    set(hm_namespace heif_hm_${hm_variant_name})

    set(hm_wrappers)
    foreach (HM_WRAPPED_SOURCE ${HM_${variant}_SOURCES})
        file(RELATIVE_PATH hm_wrapper_name "${HM_${variant}_SOURCE_DIR}" "${HM_WRAPPED_SOURCE}")
        string(REPLACE "/" "_" hm_wrapper_name "${hm_wrapper_name}")
        string(REGEX REPLACE "\\.cpp$" ".cc" hm_wrapper_name "${hm_wrapper_name}")

        # configure_file() leaves the file alone when its content does not change.
        configure_file(hm_source_wrapper.cc.in "${CMAKE_CURRENT_BINARY_DIR}/hm-${hm_variant_name}/${hm_wrapper_name}" @ONLY)
        list(APPEND hm_wrappers "${CMAKE_CURRENT_BINARY_DIR}/hm-${hm_variant_name}/${hm_wrapper_name}")
    endforeach ()

    # The code of HM. It is compiled like HM compiles it: as C++11. The warnings are disabled,
    # there are many of them with compilers that are newer than HM.
    #
    # The sanitizers of a libheif build are not applied to HM. We take HM as it is, like a
    # library that is installed on the system, and it does not pass them: it shifts negative
    # values, and the version with the screen content coding tools reads the element in front
    # of the table g_quantScales when the palette mode is used for lossless coding
    # (TEncSearch::xCalcPixelPred(); the value is not used).
    #
    # For the same reason, HM is optimized in debug builds of libheif too. Without
    # optimization, it takes four times as long.
    add_library(${hm_namespace} OBJECT ${hm_wrappers} hm_as_library.h)
    set_target_properties(${hm_namespace} PROPERTIES CXX_STANDARD 11)
    if (MSVC)
        target_compile_options(${hm_namespace} PRIVATE /w)
    else ()
        target_compile_options(${hm_namespace} PRIVATE -w -Wno-error -fno-sanitize=all $<$<CONFIG:Debug>:-O2>)
    endif ()

    # Our code that uses HM.
    add_library(${hm_namespace}_interface OBJECT encoder_hm_variant.cc encoder_hm_variant.h hm_as_library.h)
    target_compile_definitions(${hm_namespace}_interface PRIVATE
            HEIF_HM_VARIANT_FUNCTION=${function}
            ${HM_${variant}_DEFINITIONS})
    target_include_directories(${hm_namespace}_interface PRIVATE
            ${libheif_SOURCE_DIR}/libheif/api
            ${libheif_BINARY_DIR})

    foreach (hm_target ${hm_namespace} ${hm_namespace}_interface)
        set_target_properties(${hm_target} PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                CXX_VISIBILITY_PRESET hidden
                VISIBILITY_INLINES_HIDDEN ON)
        # NDEBUG: an assertion of HM must not end the process. It is defined in all build types
        # and for our code that uses HM too, which sees the inline functions of the HM headers.
        target_compile_definitions(${hm_target} PRIVATE
                HEIF_HM_NAMESPACE=${hm_namespace}
                RExt__HIGH_BIT_DEPTH_SUPPORT=1
                NDEBUG)
        target_include_directories(${hm_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

        # The HM headers are full of warnings, so their directories are declared as system directories.
        target_include_directories(${hm_target} SYSTEM PRIVATE ${HM_${variant}_INCLUDE_DIRS})

        list(APPEND HM_sources $<TARGET_OBJECTS:${hm_target}>)
    endforeach ()

    list(APPEND HM_variant_definitions HAVE_HM_VARIANT_${variant}=1)
endmacro()

if (HM_FOUND AND WITH_HM)
    set(HM_sources encoder_hm.cc encoder_hm.h encoder_hm_variant.h)
    set(HM_extra_plugin_sources)
    set(HM_variant_definitions)

    if (ENABLE_HM_VARIANT_LATEST)
        hm_variant(LATEST get_hm_variant_latest)
    endif ()
    if (ENABLE_HM_VARIANT_SCC)
        hm_variant(SCC get_hm_variant_scc)
    endif ()
endif ()
plugin_compilation(hm HM HM_FOUND HM HM)
if (HM_FOUND AND WITH_HM)
    if (TARGET heif-hm)
        target_compile_definitions(heif-hm PRIVATE ${HM_variant_definitions})
    else ()
        target_compile_definitions(heif PRIVATE ${HM_variant_definitions})
    endif ()
endif ()

set(FFMPEG_DECODER_sources decoder_ffmpeg.cc decoder_ffmpeg.h)
set(FFMPEG_DECODER_extra_plugin_sources ../error.cc nalu_utils.cc)
plugin_compilation(ffmpegdec FFMPEG FFMPEG_FOUND FFMPEG_DECODER FFMPEG_DECODER)

set(OPENJPH_ENCODER_sources encoder_openjph.cc encoder_openjph.h)
set(OPENJPH_ENCODER_extra_plugin_sources)
plugin_compilation(jphenc OPENJPH OPENJPH_FOUND OPENJPH_ENCODER OPENJPH_ENCODER)

set(UVG266_sources encoder_uvg266.cc encoder_uvg266.h)
set(UVG266_extra_plugin_sources)
plugin_compilation(uvg266 UVG266 UVG266_FOUND UVG266 UVG266)

set(VVDEC_sources decoder_vvdec.cc decoder_vvdec.h)
set(VVDEC_extra_plugin_sources)
plugin_compilation(vvdec vvdec vvdec_FOUND VVDEC VVDEC)

set(VVENC_sources encoder_vvenc.cc encoder_vvenc.h)
set(VVENC_extra_plugin_sources)
plugin_compilation(vvenc vvenc vvenc_FOUND VVENC VVENC)

set(X264_sources encoder_x264.h encoder_x264.cc)
set(X264_extra_plugin_sources)
plugin_compilation(x264 X264 X264_FOUND X264 X264)

set(OpenH264_DECODER_sources decoder_openh264.cc decoder_openh264.h)
set(OpenH264_DECODER_extra_plugin_sources)
plugin_compilation(openh264dec OpenH264 OpenH264_DECODER_FOUND OpenH264_DECODER OpenH264_DECODER)

target_sources(heif PRIVATE
               encoder_mask.h
               encoder_mask.cc
               nalu_utils.h
               nalu_utils.cc)

if (WITH_UNCOMPRESSED_CODEC)
    target_sources(heif PRIVATE
            encoder_uncompressed.h
            encoder_uncompressed.cc
            decoder_uncompressed.h
            decoder_uncompressed.cc)
endif ()

if (WITH_WEBCODECS)
   target_sources(heif PRIVATE
   		  decoder_webcodecs.h
		  decoder_webcodecs.cc)
   target_compile_definitions(heif PRIVATE HAVE_WEBCODECS=1)
endif()


