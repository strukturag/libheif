if(WITH_EXAMPLES AND TARGET PNG::PNG)
  install(FILES heif.thumbnailer DESTINATION ${CMAKE_INSTALL_DATADIR}/thumbnailers)
endif()
