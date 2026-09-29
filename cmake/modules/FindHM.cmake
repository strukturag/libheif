# The HEVC reference software HM, built for high bit depths by third-party/hm.cmd.

include(LibFindHM)

set(HM_ROOT "${PROJECT_SOURCE_DIR}/third-party/HM" CACHE PATH
        "Source tree of the HEVC reference software HM, built as third-party/hm.cmd does")

libfind_hm(HM "${HM_ROOT}" TRUE)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HM
        REQUIRED_VARS
        HM_SOURCE_DIR
        HM_TLibEncoder_LIBRARY
        HM_TLibCommon_LIBRARY
        HM_Utilities_LIBRARY
        HM_BUILD_OK
        )
