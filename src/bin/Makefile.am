AM_CPPFLAGS = -I$(top_srcdir)/src/include
AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = nxweb
nxweb_SOURCES = main.c
nxweb_LDADD = -lnxweb $(NXWEB_EXT_LIBS)
nxweb_LDFLAGS = -L$(top_builddir)/src/lib

bin_SCRIPTS = nxwebc
CLEANFILES = $(bin_SCRIPTS)
