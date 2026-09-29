# Locates the sources of one version of the HEVC reference software HM.
#
# HM has no install target and no package description, and libheif does not use a build
# of HM: it compiles the HM sources itself (see libheif/plugins/CMakeLists.txt). So all
# that is needed is the source tree, as third-party/hm.cmd and third-party/hm-scc.cmd
# download it.
#
#   libfind_hm_sources(<prefix> <root> <needs_scc>)
#
#   prefix     prefix of the result variables
#   root       source tree of HM
#   needs_scc  whether this has to be a version of HM with the screen content coding tools
#
# Result:
#   <prefix>_SOURCE_DIR    the directory "source" of HM
#   <prefix>_VERSION       version of HM, for example "18.0" or "16.21_SCM8.8"
#   <prefix>_VERSION_OK    whether the version fits to <needs_scc>
#   <prefix>_INCLUDE_DIRS  for the HM headers
#   <prefix>_SOURCES       the source files of the encoder. This includes the two classes of
#                          the HM application that hold the configuration of the encoder.
#   <prefix>_DEFINITIONS   compile definitions for the code that uses HM

macro(libfind_hm_sources prefix root needs_scc)
    find_path(${prefix}_SOURCE_DIR
            NAMES Lib/TLibEncoder/TEncTop.h
            PATHS ${root}/source
            NO_DEFAULT_PATH)

    set(${prefix}_VERSION)
    set(${prefix}_VERSION_OK FALSE)

    if (${prefix}_SOURCE_DIR)
        file(STRINGS "${${prefix}_SOURCE_DIR}/Lib/TLibCommon/CommonDef.h" ${prefix}_VERSION_LINE
                REGEX "^#define[ \t]+NV_VERSION[ \t]+\"")
        string(REGEX REPLACE "^#define[ \t]+NV_VERSION[ \t]+\"([^\"]*)\".*$" "\\1"
                ${prefix}_VERSION "${${prefix}_VERSION_LINE}")

        # The versions with the screen content coding tools are named like "16.21_SCM8.8".
        if ("${${prefix}_VERSION}" MATCHES "SCM")
            set(${prefix}_HAS_SCC TRUE)
        else ()
            set(${prefix}_HAS_SCC FALSE)
        endif ()

        if (${needs_scc} AND NOT ${prefix}_HAS_SCC)
            message(WARNING "HM ${${prefix}_VERSION} in ${root} has no screen content coding tools. Use third-party/hm-scc.cmd to download a version that has them.")
        else ()
            set(${prefix}_VERSION_OK TRUE)
        endif ()

        set(${prefix}_INCLUDE_DIRS
                ${${prefix}_SOURCE_DIR}/Lib
                ${${prefix}_SOURCE_DIR}/Lib/TLibCommon
                ${${prefix}_SOURCE_DIR}/Lib/libmd5
                ${${prefix}_SOURCE_DIR}/App/TAppEncoder)

        # Two members of the configuration class have changed their names between the
        # versions of HM.
        file(STRINGS "${${prefix}_SOURCE_DIR}/App/TAppEncoder/TAppEncCfg.h" ${prefix}_OLD_MEMBER_NAMES
                REGEX "m_iSourceWidth")
        if (${prefix}_OLD_MEMBER_NAMES)
            set(${prefix}_DEFINITIONS HEIF_HM_SOURCE_WIDTH=m_iSourceWidth HEIF_HM_SOURCE_HEIGHT=m_iSourceHeight)
        else ()
            set(${prefix}_DEFINITIONS HEIF_HM_SOURCE_WIDTH=m_sourceWidth HEIF_HM_SOURCE_HEIGHT=m_sourceHeight)
        endif ()

        if (${prefix}_HAS_SCC)
            list(APPEND ${prefix}_DEFINITIONS HEIF_HM_VARIANT_HAS_SCC=1)
        else ()
            list(APPEND ${prefix}_DEFINITIONS HEIF_HM_VARIANT_HAS_SCC=0)
        endif ()

        file(GLOB ${prefix}_SOURCES
                ${${prefix}_SOURCE_DIR}/Lib/TLibCommon/*.cpp
                ${${prefix}_SOURCE_DIR}/Lib/TLibEncoder/*.cpp
                ${${prefix}_SOURCE_DIR}/Lib/Utilities/*.cpp
                ${${prefix}_SOURCE_DIR}/Lib/libmd5/*.cpp)
        list(APPEND ${prefix}_SOURCES
                ${${prefix}_SOURCE_DIR}/App/TAppEncoder/TAppEncCfg.cpp
                ${${prefix}_SOURCE_DIR}/App/TAppEncoder/TAppEncTop.cpp)
    endif ()
endmacro()
