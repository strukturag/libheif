cmake_minimum_required (VERSION 3.0)

project(libheif LANGUAGES C CXX VERSION 1.16.2)

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
set(PLUGIN_DIRECTORY "${CMAKE_INSTALL_FULL_LIBDIR}/libheif" CACHE STRING "Plugin install directory")

if (ENABLE_PLUGIN_LOADING)
    set(PLUGIN_LOADING_SUPPORTED_AND_ENABLED TRUE)
    install(DIRECTORY DESTINATION ${PLUGIN_DIRECTORY} DIRECTORY_PERMISSIONS
        OWNER_WRITE OWNER_READ OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
endif()

macro(plugin_option variableName packageName displayName displayType defaultPlugin)
    option(WITH_${variableName} "Build ${displayName} ${displayType}" ON)
    option(WITH_${variableName}_PLUGIN "Build ${displayName} as a plugin" ${defaultPlugin})
    if (WITH_${variableName})
        find_package(${packageName})
    endif ()

    if (${variableName}_FOUND AND WITH_${variableName}_PLUGIN AND PLUGIN_LOADING_SUPPORTED_AND_ENABLED)
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

option(WITH_UNCOMPRESSED_CODEC "Support internal ISO/IEC 23001-17 uncompressed codec (experimental)" OFF)

# Libsharpyuv
option(WITH_LIBSHARPYUV "Build libsharpyuv" ON)
if (WITH_LIBSHARPYUV)
    find_package(libsharpyuv)
endif ()
if (LIBSHARPYUV_FOUND)
    message("libsharpyuv: found")
elseif (WITH_${variableName})
    message("libsharpyuv: not found")
else()
    message("libsharpyuv: disabled")
endif ()

# --- Create libheif pkgconfig file

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix "\${prefix}")
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(libdir "${CMAKE_INSTALL_LIBDIR}")
else()
    set(libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
endif()
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(includedir "${CMAKE_INSTALL_INCLUDEDIR}")
else()
    set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
endif()
if (LIBDE265_FOUND AND NOT WITH_LIBDE265_PLUGIN)
    list(APPEND REQUIRES_PRIVATE "libde265")
endif()
if (X265_FOUND AND NOT WITH_X265_PLUGIN)
    list(APPEND REQUIRES_PRIVATE "x265")
endif()
if ((AOM_DECODER_FOUND AND NOT WITH_AOM_DECODER_PLUGIN) OR (AOM_ENCODER_FOUND AND NOT WITH_AOM_ENCODER_PLUGIN))
    list(APPEND REQUIRES_PRIVATE "aom")
endif()
if (DAV1D_FOUND AND NOT WITH_DAV1D_PLUGIN)
    list(APPEND REQUIRES_PRIVATE "dav1d")
endif()
if (RAV1E_FOUND AND NOT WITH_RAV1E_PLUGIN)
    list(APPEND REQUIRES_PRIVATE "rav1e")
endif()
if (SvtEnc_FOUND AND NOT WITH_SvtEnc_PLUGIN)
    list(APPEND REQUIRES_PRIVATE "SvtAv1Enc")
endif()
if (LIBSHARPYUV_FOUND)
    list(APPEND REQUIRES_PRIVATE "libsharpyuv")
endif()
list(JOIN REQUIRES_PRIVATE " " REQUIRES_PRIVATE)

configure_file(libheif.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libheif.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libheif.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# ---

option(WITH_EXAMPLES "Build examples" ON)
option(WITH_GDK_PIXBUF "Build gdk-pixbuf plugin" ON)

option(WITH_REDUCED_VISIBILITY "Reduced symbol visibility in library" ON)

option(WITH_DEFLATE_HEADER_COMPRESSION OFF)
option(ENABLE_MULTITHREADING_SUPPORT "Switch off for platforms without multithreading support" ON)
option(ENABLE_PARALLEL_TILE_DECODING "Will launch multiple decoders to decode tiles in parallel (requires ENABLE_MULTITHREADING_SUPPORT)" ON)

if (WITH_REDUCED_VISIBILITY)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
else ()
    set(CMAKE_CXX_VISIBILITY_PRESET default)
endif ()

if(WITH_EXAMPLES)
    add_subdirectory (examples)
endif()

# --- Testing

# Disable testing by default because this requires building with full symbol visibility (switched off WITH_REDUCED_VISIBILITY).
option(BUILD_TESTING "" OFF)
include(CTest)
if(BUILD_TESTING)
    # TODO: fix tests on windows.
    add_subdirectory (tests)
endif()

if (BUILD_TESTING AND WITH_REDUCED_VISIBILITY)
    message(FATAL_ERROR "Tests can only be compiled with full symbol visibility (WITH_REDUCED_VISIBILITY=OFF)")
endif()


# --- Fuzzing

option(WITH_FUZZERS "Build the fuzzers (and no other executables)" OFF)
set(FUZZING_C_COMPILER "clang" CACHE STRING "C compiler to use for fuzzing")
set(FUZZING_CXX_COMPILER "clang++" CACHE STRING "C++ compiler to use for fuzzing")
set(FUZZING_COMPILE_OPTIONS "-fsanitize=fuzzer,address,shift,integer -fno-sanitize-recover=shift,integer" CACHE STRING "Compiler options for fuzzing")
set(FUZZING_LINKER_OPTIONS "" CACHE STRING "Additional linking options for fuzzing")

if (WITH_FUZZERS)
    set(CMAKE_C_COMPILER ${FUZZING_C_COMPILER})
    set(CMAKE_CXX_COMPILER ${FUZZING_CXX_COMPILER})
    message("Using compiler: ${CMAKE_CXX_COMPILER}")
    separate_arguments(FUZZING_COMPILE_OPTIONS UNIX_COMMAND "${FUZZING_COMPILE_OPTIONS}")
    separate_arguments(FUZZING_LINKER UNIX_COMMAND "${FUZZING_LINKER_OPTIONS}")
    add_compile_options(${FUZZING_COMPILE_OPTIONS})
    add_link_options(${FUZZING_COMPILE_OPTIONS} ${FUZZING_LINKER_OPTIONS})

    add_subdirectory(fuzzing)
endif()

if (CMAKE_CXX_COMPILER MATCHES "clang\\+\\+$")
    add_compile_options(-Wno-tautological-constant-out-of-range-compare)
endif()

add_subdirectory (libheif)

if (WITH_GDK_PIXBUF)
  add_subdirectory (gdk-pixbuf)
endif()

add_subdirectory (gnome)


# --- packaging (source code)

set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME libheif-${PACKAGE_VERSION})
set(CPACK_SOURCE_IGNORE_FILES
/.git/
/.github/
/.gitignore$
/build/
/cmake-build.*/
/.deps/
/.idea/
/.clang-tidy
~$
/third-party/.*/ # only exclude the sub-directories, but keep the *.cmd files
/Testing/
/logos/
/Makefile$
/libtool$
/libheif.pc$
/config.h$
stamp-h1$
)
include(CPack)
