# The HEVC reference software HM.
#
# The HM encoder plugin can contain two versions of HM, which we call variants:
#
#   ENABLE_HM_VARIANT_LATEST  the latest version of HM (third-party/hm.cmd, HM_ROOT)
#   ENABLE_HM_VARIANT_SCC     the latest version of HM that has the screen content coding
#                             tools (third-party/hm-scc.cmd, HM_SCC_ROOT)
#
# HM is found when the sources of all enabled variants are found.
#
# Result:
#   HM_VARIANTS   list of the enabled variants: LATEST, SCC
#   HM_<variant>_SOURCE_DIR, HM_<variant>_VERSION, HM_<variant>_INCLUDE_DIRS, HM_<variant>_SOURCES

include(LibFindHM)

set(HM_ROOT "${PROJECT_SOURCE_DIR}/third-party/HM" CACHE PATH
        "Source tree of the HEVC reference software HM, as third-party/hm.cmd downloads it")
set(HM_SCC_ROOT "${PROJECT_SOURCE_DIR}/third-party/HM-SCC" CACHE PATH
        "Source tree of the HEVC reference software HM with screen content coding tools, as third-party/hm-scc.cmd downloads it")

set(HM_VARIANTS)
set(HM_REQUIRED_VARS)

if (ENABLE_HM_VARIANT_LATEST)
    libfind_hm_sources(HM_LATEST "${HM_ROOT}" FALSE)
    list(APPEND HM_VARIANTS LATEST)
    list(APPEND HM_REQUIRED_VARS HM_LATEST_SOURCE_DIR HM_LATEST_VERSION_OK)
endif ()

if (ENABLE_HM_VARIANT_SCC)
    libfind_hm_sources(HM_SCC "${HM_SCC_ROOT}" TRUE)
    list(APPEND HM_VARIANTS SCC)
    list(APPEND HM_REQUIRED_VARS HM_SCC_SOURCE_DIR HM_SCC_VERSION_OK)
endif ()

if (NOT HM_VARIANTS)
    message(WARNING "The HM encoder is enabled (WITH_HM), but none of its variants is. Enable ENABLE_HM_VARIANT_LATEST or ENABLE_HM_VARIANT_SCC.")
endif ()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HM
        REQUIRED_VARS
        HM_VARIANTS
        ${HM_REQUIRED_VARS}
        )

find_package(Threads)
set(HM_LIBRARIES Threads::Threads)
