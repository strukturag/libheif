include_directories(${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif ${libheif_SOURCE_DIR}/libheif/api)


add_executable(box_fuzzer box_fuzzer.cc)
target_link_libraries(box_fuzzer PRIVATE heif)

add_executable(color_conversion_fuzzer color_conversion_fuzzer.cc)
target_link_libraries(color_conversion_fuzzer PRIVATE heif)

add_executable(encoder_fuzzer encoder_fuzzer.cc)
target_link_libraries(encoder_fuzzer PRIVATE heif)
configure_file(encoder_fuzzer.options ${CMAKE_CURRENT_BINARY_DIR}/encoder_fuzzer.options COPYONLY)
configure_file(encoder_fuzzer_lsan_suppressions.txt ${CMAKE_CURRENT_BINARY_DIR}/encoder_fuzzer_lsan_suppressions.txt COPYONLY)

add_executable(file_fuzzer file_fuzzer.cc)
target_link_libraries(file_fuzzer PRIVATE heif)

add_executable(api_fuzzer api_fuzzer.cc)
target_link_libraries(api_fuzzer PRIVATE heif)
