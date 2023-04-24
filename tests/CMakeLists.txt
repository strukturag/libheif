# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})

macro(add_libheif_test TEST_FILE)
    set(TEST_NAME ${TEST_FILE})
    add_executable(${TEST_NAME} main.cc catch.hpp ${TEST_FILE}.cc)
    target_link_libraries(${TEST_NAME} heif_internal)
    add_test(NAME ${TEST_NAME} COMMAND ./${TEST_NAME})
endmacro()

add_libheif_test(conversion)
add_libheif_test(encode)
