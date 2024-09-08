include_directories(${libheif_SOURCE_DIR}/examples ${libheif_SOURCE_DIR}/libheif/api)

set(LIBHEIFIO_TESTING_DATA_DIRECTORY "${CMAKE_SOURCE_DIR}/examples/tests/data")
configure_file(test-config.cc.in ${CMAKE_BINARY_DIR}/examples/tests/generated/test-config.cc)

macro(add_libheifio_test TEST_FILE)
    set(TEST_NAME ${TEST_FILE})
    add_executable(${TEST_NAME} main.cc catch.hpp ${CMAKE_BINARY_DIR}/examples/tests/generated/test-config.cc test_utils.cc ${TEST_FILE}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE heifio PRIVATE heif)
    add_test(NAME ${TEST_NAME} COMMAND ./${TEST_NAME})
endmacro()

add_libheifio_test(tiffdecode)

