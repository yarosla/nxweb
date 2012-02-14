#CFLAGS = -pthread -Wno-deprecated-declarations -O2 -g
AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

INCLUDES = -I$(top_srcdir)/src/include -I$(top_srcdir)/src/include/nxweb

lib_LIBRARIES = libnxweb.a

LIBNXWEB_HFILES =

LIBNXWEB_FILES = \
	cache.c daemon.c http_server.c \
	http_utils.c mime.c misc.c nx_buffer.c \
	nxd_buffer.c nxd_http_client_proto.c nxd_http_proxy.c \
	nxd_http_server_proto.c nxd_socket.c nxd_ssl_socket.c \
	nx_event.c nx_file_reader.c nx_pool.c nx_workers.c \
	\
	modules/http_proxy.c modules/sendfile.c \
	\
	deps/ulib/hash.c \
	deps/sha1-c/sha1.c

if WITH_ZLIB
LIBNXWEB_FILES += filters/gzip_filter.c
endif

if WITH_IMAGEMAGICK
LIBNXWEB_FILES += filters/image_filter.c
endif

libnxweb_a_SOURCES = $(LIBNXWEB_HFILES) $(LIBNXWEB_FILES)

# Pkg-config script.
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = nxweb.pc
DISTCLEANFILES = $(pkgconfig_DATA)
