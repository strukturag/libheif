set(pytest_url https://github.com/buddly27/pytest-cmake/archive/main.zip)

# Fetch CMake files from the main branch of the Github repository
file(DOWNLOAD ${pytest_url} ${CMAKE_BINARY_DIR}/regression/pytest.zip)
file(
    ARCHIVE_EXTRACT INPUT ${CMAKE_BINARY_DIR}/regression/pytest.zip
    DESTINATION ${CMAKE_BINARY_DIR}/regression
    PATTERNS "*.cmake"
)

# Expand the module path variable to discover the `FindPytest.cmake` module.
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_BINARY_DIR}/regression/pytest-cmake-main/cmake)

find_package(Pytest REQUIRED)

set(TESTING_DATA_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data")
configure_file(test_data.json.in ${CMAKE_BINARY_DIR}/regression/test_data.json)
configure_file(version.json.in ${CMAKE_BINARY_DIR}/regression/version.json)

add_test(
    NAME Regression
    COMMAND Pytest::Pytest ${CMAKE_CURRENT_SOURCE_DIR}
)

