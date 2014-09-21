ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = configure sample_config/www/index.htm sample_config/www/watermark.png sample_config/www/pic.jpg \
	     sample_config/www/base.thtml sample_config/www/page1.thtml \
	     sample_config/www/ssi.shtml sample_config/www/ssi_with_cache.shtml \
	     sample_config/python/nxwebpy.py sample_config/python/hello.py \
	     sample_config/nxweb_config.json etc/nxweb_config.json

SUBDIRS = src/include src/lib src/bin sample_config/modules

if GENERATE_CERTIFICATES
SUBDIRS += sample_config/ssl
endif

# Default server config
nxwebconfigdir = $(sysconfdir)/nxweb
nxwebconfig_DATA = etc/nxweb_config.json

# Python WSGI adapter
if WITH_PYTHON
nxwebxlibdir = $(libdir)/nxweb
nxwebxlib_DATA =  sample_config/python/nxwebpy.py
endif
