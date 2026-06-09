include(LibFindMacros)
if(NOT MSVC)
    libfind_pkg_check_modules(ZIMG_PKGCONF zimg)
    find_path(ZIMG_INCLUDE_DIR
        NAMES api/zimg.h
        HINTS ${ZIMG_PKGCONF_INCLUDE_DIRS} ${ZIMG_PKGCONF_INCLUDEDIR}
        PATH_SUFFIXES ZIMG
    )
    find_library(ZIMG_LIBRARY
        NAMES zimg
        HINTS ${ZIMG_PKGCONF_LIBRARY_DIRS} ${ZIMG_PKGCONF_LIBDIR}
    )
    set(ZIMG_PROCESS_LIBS ZIMG_LIBRARY)
    set(ZIMG_PROCESS_INCLUDES ZIMG_INCLUDE_DIR)
    libfind_process(ZIMG)
else()
    # You'll need to compile zimg Visual Studio projects with the same compiler as libheif
    find_path(ZIMG_INCLUDE_DIR NAMES zimg.h HINTS ${ZIMG_PKGCONF_INCLUDE_DIRS} ${ZIMG_PKGCONF_INCLUDEDIR} ${PROJECT_SOURCE_DIR}/third-party/zimg/src/zimg/api)
    find_library(ZIMG_LIBRARY NAMES z_imp z HINTS ${ZIMG_PKGCONF_INCLUDE_DIRS} ${ZIMG_PKGCONF_INCLUDEDIR} ${PROJECT_SOURCE_DIR}/third-party/zimg/_msvc/x64/${CMAKE_BUILD_TYPE} ${PROJECT_SOURCE_DIR}/third-party/zimg/_msvc/zimg/x64/${CMAKE_BUILD_TYPE})
    set(ZIMG_PROCESS_LIBS ZIMG_LIBRARY)
    set(ZIMG_PROCESS_INCLUDES ZIMG_INCLUDE_DIR)
    libfind_process(ZIMG)
    if(ZIMG_FOUND)
        add_library(ZIMG STATIC IMPORTED)
        set_target_properties(ZIMG PROPERTIES
            IMPORTED_LOCATION "${ZIMG_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ZIMG_INCLUDE_DIR}"
        )
        if(ZIMG_LIBRARY MATCHES .+/z_imp\.lib$)
            # --- Using shared build, copy the DLL into the executable directory for easier development
            string(REPLACE z_imp.lib z.dll ZIMG_SHARED_LIB ${ZIMG_LIBRARY})
            add_custom_command(TARGET heif POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                ${ZIMG_SHARED_LIB}
                $<TARGET_FILE_DIR:heif>/../examples
            )
        endif()
    else()
        message(ZIMG not found. To add ZIMG color conversion support, clone https://github.com/sekrit-twc/zimg/ into third-party/zimg, then compile zimg.sln found in _msvc subdirectory with the same Visual Studio version.)
    endif()
endif()
