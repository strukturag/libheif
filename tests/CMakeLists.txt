# Needed to find libheif/heif_version.h while compiling the library
include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR})

# prepare C++ configuration file (test-config.cc)

set(TESTING_DATA_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data")
configure_file(test-config.cc.in ${CMAKE_BINARY_DIR}/generated/test-config.cc)

macro(add_libheif_test TEST_FILE)
    set(TEST_NAME ${TEST_FILE})
    add_executable(${TEST_NAME} main.cc catch.hpp ${CMAKE_BINARY_DIR}/generated/test-config.cc ${TEST_FILE}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE heif)
    add_test(NAME ${TEST_NAME} COMMAND ./${TEST_NAME})
endmacro()

add_libheif_test(conversion)
add_libheif_test(encode)
add_libheif_test(uncompressed_decode)

