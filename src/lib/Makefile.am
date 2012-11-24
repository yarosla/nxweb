#CFLAGS = -pthread -Wno-deprecated-declarations -O2 -g
AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

INCLUDES = -I$(top_srcdir)/src/include -I$(top_srcdir)/src/include/nxweb

lib_LTLIBRARIES = libnxweb.la

libnxweb_la_SOURCES = \
	cache.c daemon.c http_server.c \
	http_utils.c mime.c misc.c nx_buffer.c \
	nxd_buffer.c nxd_http_client_proto.c nxd_http_proxy.c \
	nxd_http_server_proto.c nxd_http_server_proto_subrequest.c \
	nxd_socket.c nxd_ssl_socket.c nxd_streamer.c \
	nx_event.c nx_file_reader.c nx_pool.c nx_workers.c \
	http_subrequest.c templates.c \
	\
	modules/http_proxy.c modules/sendfile.c \
	\
	filters/ssi_filter.c \
	filters/templates_filter.c \
	filters/file_cache_filter.c \
	\
	deps/ulib/hash.c \
	deps/sha1-c/sha1.c

if WITH_ZLIB
libnxweb_la_SOURCES += filters/gzip_filter.c
endif

if WITH_IMAGEMAGICK
libnxweb_la_SOURCES += filters/image_filter.c
libnxweb_la_SOURCES += filters/draw_filter.c
endif

libnxweb_la_LDFLAGS = -version-info $(NXWEB_LIB_VERSION_INFO)

# Pkg-config script.
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nxweb.pc
DISTCLEANFILES = $(pkgconfig_DATA)
