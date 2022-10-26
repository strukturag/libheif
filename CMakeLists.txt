cmake_minimum_required (VERSION 3.0)

project(libheif LANGUAGES C CXX VERSION 1.13.0.0)

# https://cmake.org/cmake/help/v3.1/policy/CMP0054.html
cmake_policy(VERSION 3.0...3.22)
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
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      add_definitions(-Wno-error=tautological-compare)
      add_definitions(-Wno-error=tautological-constant-out-of-range-compare)
  endif ()
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

LIST (APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

option(ENABLE_PLUGIN_LOADING "Support loading of plugins" ON)
set(PLUGIN_DIRECTORY "/usr/local/lib/libheif" CACHE STRING "Plugin install directory")

macro(plugin_option variableName packageName displayName displayType defaultPlugin)
    option(WITH_${variableName} "Build ${displayName} ${displayType}" ON)
    option(WITH_${variableName}_PLUGIN "Build ${displayName} as a plugin" ${defaultPlugin})
    if (WITH_${variableName})
        find_package(${packageName})
    endif ()

    if (${variableName}_FOUND AND WITH_${variableName}_PLUGIN)
        set(msg "found (plugin)")
    elseif (${variableName}_FOUND)
        set(msg "found (built-in)")
    elseif (WITH_${variableName})
        set(msg "not found")
    else()
        set(msg "disabled")
    endif ()

    message("${displayName} (${displayType}): ${msg}")
    unset(msg)
endmacro()

plugin_option(LIBDE265 LIBDE265 "libde265" "HEIC decoder" OFF)
plugin_option(X265 X265 "x265" "HEIC encoder" OFF)
plugin_option(DAV1D DAV1D "Dav1d" "AVIF decoder" OFF)
plugin_option(AOM_ENCODER AOM "aom" "AVIF encoder" OFF)
plugin_option(AOM_DECODER AOM "aom" "AVIF decoder" OFF)
plugin_option(SvtEnc SvtEnc "Svt-av1" "AVIF encoder" ON)
plugin_option(RAV1E RAV1E "Rav1e" "AVIF encoder" ON)


# --- Create libheif pkgconfig file

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${CMAKE_INSTALL_PREFIX})
set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})
if (LIBDE265_FOUND)
    list(APPEND REQUIRES_PRIVATE "libde265")
    set(have_libde265 yes)
else()
    set(have_libde265 no)
endif()
if (X265_FOUND)
    list(APPEND REQUIRES_PRIVATE "x265")
    set(have_x265 yes)
else()
    set(have_x265 no)
endif()
if (AOM_DECODER_FOUND OR AOM_ENCODER_FOUND)
    list(APPEND REQUIRES_PRIVATE "aom")
endif()
if (DAV1D_FOUND)
    list(APPEND REQUIRES_PRIVATE "dav1d")
endif()
if (RAV1E_FOUND)
    list(APPEND REQUIRES_PRIVATE "rav1e")
endif()
if (SvtEnc_FOUND)
    list(APPEND REQUIRES_PRIVATE "SvtEnc")
endif()
if (AOM_DECODER_FOUND OR DAV1D_FOUND)
    set(have_avif_decoder yes)
else()
    set(have_avif_decoder no)
endif()
if (AOM_ENCODER_FOUND OR RAV1E_FOUND)
    set(have_avif_encoder yes)
else()
    set(have_avif_encoder no)
endif()
list(JOIN REQUIRES_PRIVATE " " REQUIRES_PRIVATE)
set(VERSION ${PROJECT_VERSION})

configure_file(libheif.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libheif.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libheif.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# ---

option(WITH_EXAMPLES "Build examples" ON)

option(WITH_DEFLATE_HEADER_COMPRESSION OFF)

if(WITH_EXAMPLES)
    add_subdirectory (examples)
endif()
add_subdirectory (libheif)
add_subdirectory (gdk-pixbuf)
add_subdirectory (gnome)
