cmake_minimum_required (VERSION 3.3.2)

project (libheif
    LANGUAGES C CXX
    VERSION 1.6.0
)

# https://cmake.org/cmake/help/v3.1/policy/CMP0054.html
cmake_policy(SET CMP0054 NEW)
include(GNUInstallDirs)

# The version number.
set (NUMERIC_VERSION 0x01060000)
set (PACKAGE_VERSION 1.6.0)

include (${CMAKE_ROOT}/Modules/CheckCCompilerFlag.cmake)
include (${CMAKE_ROOT}/Modules/CheckIncludeFile.cmake)

CHECK_INCLUDE_FILE(inttypes.h HAVE_INTTYPES_H)
CHECK_INCLUDE_FILE(stddef.h HAVE_STDDEF_H)
CHECK_INCLUDE_FILE(strings.h HAVE_STRINGS_H)
CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)

if (HAVE_INTTYPES_H)
  add_definitions(-DHAVE_INTTYPES_H)
endif()
if (HAVE_STDDEF_H)
  add_definitions(-DHAVE_STDDEF_H)
endif()
if (HAVE_STRINGS_H)
  add_definitions(-DHAVE_STRINGS_H)
endif()
if (HAVE_UNISTD_H)
  add_definitions(-DHAVE_UNISTD_H)
endif()

configure_file ("libheif/heif_version.h.in" "libheif/heif_version.h")

if(NOT MSVC)
  add_definitions(-Wall)
  add_definitions(-Werror)
  add_definitions(-Wsign-compare)
  add_definitions(-Wconversion)
  add_definitions(-Wno-sign-conversion)
  add_definitions(-Wno-error=conversion)
  add_definitions(-Wno-error=unused-parameter)
  add_definitions(-Wno-error=deprecated-declarations)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wno-error=potentially-evaluated-expression has_potentially_evaluated_expression)
if (has_potentially_evaluated_expression)
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=potentially-evaluated-expression")
  add_definitions(-Wno-error=potentially-evaluated-expression)
endif()

include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (examples)
add_subdirectory (libheif)
add_subdirectory (gdk-pixbuf)
