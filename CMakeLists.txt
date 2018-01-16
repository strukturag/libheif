project (libheif)
cmake_minimum_required (VERSION 2.8.8)

# https://cmake.org/cmake/help/v3.1/policy/CMP0054.html
cmake_policy(SET CMP0054 NEW)

# The version number.
set (NUMERIC_VERSION 0x00000100)
set (PACKAGE_VERSION 0.0.1)

include (${CMAKE_ROOT}/Modules/CheckCCompilerFlag.cmake)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/heif-version.h.in"
  "${PROJECT_SOURCE_DIR}/src/heif-version.h"
)

if(CMAKE_COMPILER_IS_GNUCXX)
  set(GCC 1)
  add_definitions(-Wall)
  add_definitions(-Werror)
  set(CMAKE_CXX_FLAGS "-std=gnu++0x ${CMAKE_CXX_FLAGS}")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  add_definitions(-Wall)
  set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
endif()

include_directories ("${PROJECT_SOURCE_DIR}/src")

if(UNIX)
  set(LIBHEIF_LIBRARY_NAME heif)
else()
  set(LIBHEIF_LIBRARY_NAME libheif)
endif()

add_subdirectory (src)
