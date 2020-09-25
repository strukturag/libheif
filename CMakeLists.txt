cmake_minimum_required (VERSION 3.3.2)

project(libheif LANGUAGES C CXX VERSION 1.9.1.0)

option(USE_LOCAL_RAV1E "Include rav1e support from local rav1e source" FALSE)

# https://cmake.org/cmake/help/v3.1/policy/CMP0054.html
cmake_policy(SET CMP0054 NEW)
include(GNUInstallDirs)

# The version number.
set (PACKAGE_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Check for unistd.h

include (${CMAKE_ROOT}/Modules/CheckIncludeFile.cmake)

CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)

if (HAVE_UNISTD_H)
  add_definitions(-DHAVE_UNISTD_H)
endif()


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

# Create the compile command database for clang by default
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wno-error=potentially-evaluated-expression has_potentially_evaluated_expression)
if (has_potentially_evaluated_expression)
  add_definitions(-Wno-error=potentially-evaluated-expression)
endif()

LIST (APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
find_package(Libde265 REQUIRED)
find_package(X265)
find_package(LibAOM)
find_package(Dav1d)

if (LIBDE265_FOUND)
    message("HEIF decoder, libde265: found")
else ()
    message("HEIF decoder, libde265: not found")
endif ()

if (X265_FOUND)
    message("HEIF encoder, x265: found")
else ()
    message("HEIF encoder, x265: not found")
endif ()

if (AOM_FOUND)
    message("AVIF decoder/encoder, aom: found")
else ()
    message("AVIF decoder/encoder, aom: not found")
endif ()

if (USE_LOCAL_RAV1E)
    message("AVIF encoder, use local rav1e: yes")
else ()
    message("AVIF encoder, use local rav1e: no")
endif ()

if (DAV1D_FOUND)
    message("AVIF decoder, dav1d: found")
else ()
    message("AVIF decoder, dav1d: not found")
endif ()


# Create libheif pkgconfig file
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${CMAKE_INSTALL_PREFIX})
set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})
if (LIBDE265_FOUND)
    set(have_libde265 yes)
else()
    set(have_libde265 no)
endif()
if (X265_FOUND)
    set(have_x265 yes)
else()
    set(have_x265 no)
endif()
if (AOM_FOUND OR DAV1D_FOUND)
    set(have_avif_decoder yes)
else()
    set(have_avif_decoder no)
endif()
if (AOM_FOUND OR USE_LOCAL_RAV1E)
    set(have_avif_encoder yes)
else()
    set(have_avif_encoder no)
endif()
set(VERSION ${PROJECT_VERSION})

configure_file(libheif.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libheif.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libheif.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

option(WITH_EXAMPLES "Build examples" ON)

if(WITH_EXAMPLES)
    add_subdirectory (examples)
endif()
add_subdirectory (libheif)
add_subdirectory (gdk-pixbuf)
