include(FindPackageHandleStandardArgs)

find_path(BROTLI_DEC_INCLUDE_DIR "brotli/decode.h")
find_path(BROTLI_ENC_INCLUDE_DIR "brotli/encode.h")

find_library(BROTLI_COMMON_LIB NAMES brotlicommon)
find_library(BROTLI_DEC_LIB NAMES brotlidec)
find_library(BROTLI_ENC_LIB NAMES brotlienc)

find_package_handle_standard_args(Brotli
  FOUND_VAR
    Brotli_FOUND
  REQUIRED_VARS
    BROTLI_COMMON_LIB
    BROTLI_DEC_INCLUDE_DIR
    BROTLI_DEC_LIB
    BROTLI_ENC_INCLUDE_DIR
    BROTLI_ENC_LIB
  FAIL_MESSAGE
    "Did not find Brotli"
)


set(HAVE_BROTLI ${Brotli_FOUND})
set(BROTLI_INCLUDE_DIRS ${BROTLI_DEC_INCLUDE_DIR} ${BROTLI_ENC_INCLUDE_DIR})
set(BROTLI_LIBS "${BROTLICOMMON_LIBRARY}" "${BROTLI_DEC_LIB}"  "${BROTLI_ENC_LIB}")