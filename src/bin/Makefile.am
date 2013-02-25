INCLUDES = -I$(top_srcdir)/src/include
AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

bin_PROGRAMS = nxweb
nxweb_SOURCES = main.c modules/benchmark.c modules/hello.c modules/upload.c modules/subrequests.c
nxweb_LDADD = -lnxweb $(NXWEB_EXT_LIBS)
nxweb_LDFLAGS = -L$(top_builddir)/src/lib

if WITH_PYTHON
nxweb_SOURCES += modules/python.c
AM_CFLAGS += $(PYTHON_CPPFLAGS)
nxweb_LDFLAGS += $(PYTHON_LDFLAGS)
endif
