cmake_minimum_required(VERSION 2.8.4)

project(nxweb_lib)

set(LIB_SOURCE_FILES cache.c daemon.c http_server.c
  http_utils.c mime.c misc.c nx_buffer.c
  nxd_buffer.c nxd_http_client_proto.c nxd_http_proxy.c
  nxd_http_server_proto.c nxd_http_server_proto_subrequest.c
  nxd_socket.c nxd_ssl_socket.c nxd_streamer.c
  nx_event.c nx_file_reader.c nx_pool.c nx_workers.c
  http_subrequest.c templates.c access_log.c main_stub.c
  nxjson.c json_config.c

  modules/http_proxy.c modules/sendfile.c modules/host_redirect.c

  filters/ssi_filter.c
  filters/templates_filter.c
  filters/file_cache_filter.c
  filters/cors_filter.c

  deps/ulib/hash.c
  deps/sha1-c/sha1.c)

if(WITH_GZIP)
  list(APPEND LIB_SOURCE_FILES filters/gzip_filter.c)
endif(WITH_GZIP)

if (WITH_IMAGEMAGICK)
  list(APPEND LIB_SOURCE_FILES filters/image_filter.c filters/draw_filter.c)
endif (WITH_IMAGEMAGICK)

if (WITH_PYTHON)
  list(APPEND LIB_SOURCE_FILES modules/python.c)
endif (WITH_PYTHON)


include_directories(../include ../include/nxweb ${EXTRA_INCLUDES})

#set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-pthread -O2 -g")
add_compile_options(-pthread)


add_library(nxweb_so SHARED ${LIB_SOURCE_FILES})
set_target_properties(nxweb_so PROPERTIES SOVERSION 0.0.0)
set_target_properties(nxweb_so PROPERTIES OUTPUT_NAME nxweb)

# add_library(nxweb_static STATIC ${LIB_SOURCE_FILES})
# set_target_properties(nxweb_static PROPERTIES OUTPUT_NAME nxweb)


install(TARGETS nxweb_so DESTINATION lib)
# install(TARGETS nxweb_static DESTINATION lib)
install(DIRECTORY ../include/nxweb DESTINATION include FILES_MATCHING PATTERN "*.h")

add_subdirectory(pkgconfig)
