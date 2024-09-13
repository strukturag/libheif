include(LibFindMacros)

find_library(NVDEC_LIBRARY
    NAMES libnvcuvid nvcuvid
)

find_package(CUDAToolkit)

set(NVDEC_PROCESS_LIBS NVDEC_LIBRARY)
libfind_process(NVDEC)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NVDEC
    REQUIRED_VARS
        NVDEC_LIBRARY
)
