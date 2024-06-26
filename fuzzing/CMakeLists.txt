include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif ${libheif_SOURCE_DIR}/libheif/api)


add_executable(box_fuzzer box_fuzzer.cc)
target_link_libraries(box_fuzzer PRIVATE heif)

add_executable(color_conversion_fuzzer color_conversion_fuzzer.cc)
target_link_libraries(color_conversion_fuzzer PRIVATE heif)

add_executable(encoder_fuzzer encoder_fuzzer.cc)
target_link_libraries(encoder_fuzzer PRIVATE heif)

add_executable(file_fuzzer file_fuzzer.cc)
target_link_libraries(file_fuzzer PRIVATE heif)
