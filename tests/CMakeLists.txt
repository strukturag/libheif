# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})

# prepare C++ configuration file (test-config.cc)

set(TESTING_DATA_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data")
configure_file(test-config.cc.in ${CMAKE_BINARY_DIR}/generated/test-config.cc)

macro(add_libheif_test TEST_FILE)
    set(TEST_NAME ${TEST_FILE})
    add_executable(${TEST_NAME} main.cc catch.hpp ${CMAKE_BINARY_DIR}/generated/test-config.cc test_utils.cc ${TEST_FILE}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE heif)
    add_test(NAME ${TEST_NAME} COMMAND ./${TEST_NAME})
endmacro()

# --- tests that require access to internal symbols

if (WITH_REDUCED_VISIBILITY)
    message(WARNING "Conversion and JPEG 2000 box unit tests can only be compiled with full symbol visibility (WITH_REDUCED_VISIBILITY=OFF)")
else()
    add_libheif_test(conversion)
    add_libheif_test(jpeg2000)
endif()

# --- tests that only access the public API

add_libheif_test(encode)
add_libheif_test(region)

if (WITH_UNCOMPRESSED_CODEC)
    add_libheif_test(uncompressed_decode)
    add_libheif_test(uncompressed_encode)
else()
    message(WARNING "Tests of the 'uncompressed codec' are not compiled because the uncompressed codec is not enabled (WITH_UNCOMPRESSED_CODEC==OFF)")
endif ()
