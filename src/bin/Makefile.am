AM_CPPFLAGS = -I$(top_srcdir)/src/include
AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = nxweb
nxweb_SOURCES = main.c modules/benchmark.c modules/hello.c modules/upload.c modules/subrequests.c
nxweb_LDADD = -lnxweb $(NXWEB_EXT_LIBS)
nxweb_LDFLAGS = -L$(top_builddir)/src/lib
