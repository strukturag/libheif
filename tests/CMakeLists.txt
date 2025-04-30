# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif ${libheif_SOURCE_DIR}/libheif/api)

# prepare C++ configuration file (test-config.cc)

set(TESTING_DATA_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(LIBHEIFIO_TESTING_DATA_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/heifio")
configure_file(test-config.cc.in ${CMAKE_BINARY_DIR}/generated/test-config.cc)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(catch_amalgamated.cpp PROPERTIES COMPILE_FLAGS -Wno-conversion)
endif()

add_library(testframework STATIC ${CMAKE_BINARY_DIR}/generated/test-config.cc test_utils.cc catch_amalgamated.cpp)
target_link_libraries(testframework PRIVATE heif)

macro(add_libheif_test TEST_FILE)
    set(TEST_NAME ${TEST_FILE})
    add_executable(${TEST_NAME} ${TEST_FILE}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE heif testframework)
    add_test(NAME ${TEST_NAME} COMMAND ./${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES SKIP_REGULAR_EXPRESSION "[1-9][0-9]* skipped")
endmacro()

macro(add_heifio_test TEST_FILE)
    set(TEST_NAME ${TEST_FILE})
    add_executable(${TEST_NAME} main.cc ${TEST_FILE}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE heif heifio testframework)
    target_include_directories(${TEST_NAME} PRIVATE ${libheif_SOURCE_DIR})
    add_test(NAME ${TEST_NAME} COMMAND ./${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES SKIP_REGULAR_EXPRESSION "[1-9][0-9]* skipped")
endmacro()

# --- tests that require access to internal symbols

if (WITH_REDUCED_VISIBILITY)
    message(WARNING "Several unit tests have been disabled because they can only be compiled with full symbol visibility (WITH_REDUCED_VISIBILITY=OFF)")
else()
    add_libheif_test(bitstream_tests)
    add_libheif_test(box_equals)
    add_libheif_test(conversion)
    add_libheif_test(idat)
    add_libheif_test(jpeg2000)
    add_libheif_test(avc_box)
    add_libheif_test(file_layout)
endif()

if (ENABLE_EXPERIMENTAL_FEATURES AND WITH_REDUCED_VISIBILITY)
    add_libheif_test(pixel_data_types)
endif()

if (NOT WITH_REDUCED_VISIBILITY AND WITH_UNCOMPRESSED_CODEC)
    add_libheif_test(uncompressed_box)
endif()

# --- tests that only access the public API

add_libheif_test(encode)
add_libheif_test(extended_type)
add_libheif_test(region)
add_libheif_test(tai)

if (WITH_OPENJPH_ENCODER AND SUPPORTS_J2K_HT_ENCODING)
    add_libheif_test(encode_htj2k)
else()
    message(INFO "Disabling HT-JPEG 2000 encoder tests because no HT-JPEG 2000 codec is enabled")
endif()

if (WITH_OpenJPEG_ENCODER AND SUPPORTS_J2K_ENCODING)
    add_libheif_test(encode_jpeg2000)
else()
    message(INFO "Disabling JPEG 2000 encoder tests because no JPEG 2000 codec is enabled")
endif()

if (ENABLE_EXPERIMENTAL_MINI_FORMAT)
    if (NOT WITH_REDUCED_VISIBILITY)
        add_libheif_test(mini_box)
    endif()
    add_libheif_test(mini_decode)
endif()

if (WITH_UNCOMPRESSED_CODEC)
    add_libheif_test(uncompressed_decode)
    add_libheif_test(uncompressed_decode_mono)
    add_libheif_test(uncompressed_decode_rgb)
    add_libheif_test(uncompressed_decode_rgb16)
    add_libheif_test(uncompressed_decode_rgb565)
    add_libheif_test(uncompressed_decode_rgb7)
    add_libheif_test(uncompressed_decode_ycbcr)
    add_libheif_test(uncompressed_decode_ycbcr420)
    add_libheif_test(uncompressed_decode_ycbcr422)
    add_libheif_test(uncompressed_encode)

    if (ZLIB_FOUND)
        add_libheif_test(uncompressed_decode_generic_compression)
    else()
        message(WARNING "Generic compress tests of the 'uncompressed codec' are not compiled because zlib was not found")
    endif ()
else()
    message(WARNING "Tests of the 'uncompressed codec' are not compiled because the uncompressed codec is not enabled (WITH_UNCOMPRESSED_CODEC==OFF)")
endif ()

add_heifio_test(tiffdecode)

if (ENABLE_PLUGIN_LOADING)
    get_directory_property(ALL_TESTS TESTS)
    set_tests_properties(${ALL_TESTS} PROPERTIES ENVIRONMENT "LIBHEIF_PLUGIN_PATH=${CMAKE_BINARY_DIR}/libheif/plugins")
endif ()
