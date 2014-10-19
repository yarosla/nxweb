#AM_CFLAGS = -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS)

#if WITH_PYTHON
#AM_CFLAGS += $(PYTHON_CPPFLAGS)
#endif

#AUTOMAKE_OPTIONS = subdir-objects

#noinst_LTLIBRARIES = sample_modules.la

#sample_modules_la_SOURCES = benchmark.c hello.c subrequests.c upload.c
#sample_modules_la_LDFLAGS = -module -version-info 0:0:0 -shared

MODULES_SRC = benchmark.c hello.c subrequests.c upload.c

EXTRA_DIST = $(MODULES_SRC)

all-local: sample_modules.so

clean-local:
	rm sample_modules.so

sample_modules.so: $(MODULES_SRC)
	gcc -I$(top_srcdir)/src/include -pthread $(GNUTLS_CFLAGS) $(IMAGEMAGICK_CFLAGS) $(PYTHON_CPPFLAGS) $(MODULES_SRC) -fPIC -shared -o sample_modules.so
