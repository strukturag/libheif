# Locates a build of the HEVC reference software HM in its source tree.
#
# HM has no install target and no package description. third-party/hm.cmd downloads
# and builds it, and this macro looks into the result. It takes the location as an
# argument, so that several versions of HM can be found.
#
#   libfind_hm(<prefix> <root> <high_bitdepth>)
#
#   prefix         prefix of the result variables
#   root           source tree of HM
#   high_bitdepth  whether HM has to be built with HIGH_BITDEPTH or without
#
# Result:
#   <prefix>_INCLUDE_DIRS   for the HM headers
#   <prefix>_LIBRARIES      the HM libraries
#   <prefix>_APP_SOURCES    the classes of the HM application that hold the
#                           configuration of the encoder. HM builds them into its
#                           executable only, so libheif compiles them itself.
#   <prefix>_BUILD_OK       whether the build has the requested HIGH_BITDEPTH setting

macro(libfind_hm prefix root high_bitdepth)
    find_path(${prefix}_SOURCE_DIR
            NAMES Lib/TLibEncoder/TEncTop.h
            PATHS ${root}/source
            NO_DEFAULT_PATH)

    # HM writes its libraries to lib/umake/<compiler>/<architecture>/<build type>
    file(GLOB ${prefix}_LIBRARY_DIRS "${root}/lib/umake/*/*/release")

    foreach (hm_library TLibEncoder TLibCommon Utilities)
        find_library(${prefix}_${hm_library}_LIBRARY
                NAMES ${hm_library}
                PATHS ${${prefix}_LIBRARY_DIRS}
                NO_DEFAULT_PATH)
    endforeach ()

    # The HIGH_BITDEPTH setting changes the data types in the HM headers. A plugin that is
    # compiled for the other setting would crash.
    set(${prefix}_BUILD_OK FALSE)
    if (EXISTS "${root}/build/CMakeCache.txt")
        file(STRINGS "${root}/build/CMakeCache.txt" ${prefix}_CACHE_LINE REGEX "^HIGH_BITDEPTH:BOOL=")
        if ("${${prefix}_CACHE_LINE}" MATCHES "=(ON|TRUE|1)$")
            set(${prefix}_IS_HIGH_BITDEPTH TRUE)
        else ()
            set(${prefix}_IS_HIGH_BITDEPTH FALSE)
        endif ()

        if ((${high_bitdepth} AND ${prefix}_IS_HIGH_BITDEPTH) OR (NOT ${high_bitdepth} AND NOT ${prefix}_IS_HIGH_BITDEPTH))
            set(${prefix}_BUILD_OK TRUE)
        else ()
            message(WARNING "HM in ${root} was built with HIGH_BITDEPTH=${${prefix}_IS_HIGH_BITDEPTH}, which is not what libheif needs here.")
        endif ()
    endif ()

    if (${prefix}_SOURCE_DIR)
        set(${prefix}_INCLUDE_DIRS
                ${${prefix}_SOURCE_DIR}/Lib
                ${${prefix}_SOURCE_DIR}/App/TAppEncoder)
        set(${prefix}_APP_SOURCES
                ${${prefix}_SOURCE_DIR}/App/TAppEncoder/TAppEncCfg.cpp
                ${${prefix}_SOURCE_DIR}/App/TAppEncoder/TAppEncTop.cpp)
    endif ()

    find_package(Threads)
    set(${prefix}_LIBRARIES
            ${${prefix}_TLibEncoder_LIBRARY}
            ${${prefix}_TLibCommon_LIBRARY}
            ${${prefix}_Utilities_LIBRARY}
            Threads::Threads)
endmacro()
