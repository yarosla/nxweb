ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = configure www/root/index.htm www/root/watermark.png www/root/pic.jpg \
	     www/root/base.thtml www/root/page1.thtml www/root/ssi.shtml \
	     python/nxwebpy.py python/hello.py nxweb_config.json

SUBDIRS = src/include src/lib src/bin
if WITH_SSL
SUBDIRS += ssl
endif
