INCLUDES = -I$(top_srcdir)/src/include
AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

bin_PROGRAMS = nxweb
nxweb_SOURCES = main.c modules/benchmark.c modules/hello.c
nxweb_LDADD = -lnxweb $(NXWEB_EXT_LIBS)
nxweb_LDFLAGS = -L$(top_builddir)/src/lib
