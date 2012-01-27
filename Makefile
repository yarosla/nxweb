BASE_NAME=nxweb

###
# List active modules here; might need to configure them in main.c
INC_MODULES=
SRC_MODULES=modules/benchmark.c modules/hello.c modules/sendfile.c

###
# Library dependencies:
#  - rt
#  - pthread
#  - gnutls (GNUTLS v.3.0.12+ is strongly recommended)

LIBS=-lrt -lpthread -lgnutls

###
# Make sure GNUTLS/bin folder is in PATH or define it here.
# This is required for SSL key generation only.
# GNUTLS v.3.0.12+ is strongly recommended.
GNUTLS_BIN_DIR=

###
# nxweb makes use of eventfd:
#   eventfd() is available on Linux since kernel 2.6.22.  Working support is
#   provided in glibc since version 2.8.  The eventfd2() system call
#   is available on Linux since kernel 2.6.27.  Since version 2.9, the glibc
#   eventfd() wrapper will employ the eventfd2() system call, if it is supported
#   by the kernel.

###
# nxweb makes use of some gcc features (builtins, attributes, etc.)

CC=gcc
LD=gcc

###
# sendfile.c module makes use of some ulib (http://code.google.com/p/ulib/)
#   components. The source code of these components is included in deps directory.

INC_DEPS=deps/ulib/alignhash_tpl.h deps/ulib/common.h deps/ulib/hash.h
SRC_DEPS=deps/ulib/hash.c

CFLAGS_RELEASE=-pthread -Wno-deprecated-declarations -O2 -s -DWITH_SSL
CFLAGS_DEBUG=-pthread -Wno-deprecated-declarations -g -DNX_DEBUG -DWITH_SSL

LDFLAGS_RELEASE=$(LIBS)
LDFLAGS_DEBUG=$(LIBS)

INC_MAIN=nxweb/config.h nxweb/http_server.h nxweb/misc.h nxweb/nx_alloc.h \
	nxweb/nx_buffer.h nxweb/nxd.h nxweb/nx_event.h nxweb/nx_file_reader.h \
	nxweb/nx_pool.h nxweb/nx_queue.h nxweb/nx_queue_tpl.h \
	nxweb/nxweb.h nxweb/nx_workers.h
SRC_MAIN=nxweb/daemon.c nxweb/http_proxy_handler.c nxweb/http_server.c \
	nxweb/http_utils.c nxweb/mime.c nxweb/misc.c nxweb/nx_buffer.c \
	nxweb/nxd_buffer.c nxweb/nxd_http_client_proto.c nxweb/nxd_http_proxy.c \
	nxweb/nxd_http_server_proto.c nxweb/nxd_socket.c nxweb/nxd_ssl_socket.c \
	nxweb/nx_event.c nxweb/nx_file_reader.c nxweb/nx_pool.c nxweb/nx_workers.c \
	main.c

OBJ_DEBUG_DIR=obj/Debug
BIN_DEBUG_DIR=bin/Debug
OBJ_RELEASE_DIR=obj/Release
BIN_RELEASE_DIR=bin/Release
OBJS_RELEASE=$(patsubst %.c,$(OBJ_RELEASE_DIR)/%.o,$(SRC_MAIN)) $(patsubst %.c,$(OBJ_RELEASE_DIR)/%.o,$(SRC_MODULES)) $(patsubst %.c,$(OBJ_RELEASE_DIR)/%.o,$(SRC_DEPS))
OBJS_DEBUG=$(patsubst %.c,$(OBJ_DEBUG_DIR)/%.o,$(SRC_MAIN)) $(patsubst %.c,$(OBJ_DEBUG_DIR)/%.o,$(SRC_MODULES)) $(patsubst %.c,$(OBJ_DEBUG_DIR)/%.o,$(SRC_DEPS))

all: Release SSL_KEYS

Release: $(BIN_RELEASE_DIR) $(OBJ_RELEASE_DIR) $(BIN_RELEASE_DIR)/$(BASE_NAME)

Debug: $(BIN_DEBUG_DIR) $(OBJ_DEBUG_DIR) $(BIN_DEBUG_DIR)/$(BASE_NAME)

modules/sendfile.o: modules/sendfile.c $(INC_DEPS)

$(BIN_RELEASE_DIR):
	mkdir -p $(BIN_RELEASE_DIR)

$(OBJ_RELEASE_DIR):
	mkdir -p $(OBJ_RELEASE_DIR)/nxweb
	mkdir -p $(OBJ_RELEASE_DIR)/modules
	mkdir -p $(OBJ_RELEASE_DIR)/deps/ulib

$(BIN_DEBUG_DIR):
	mkdir -p $(BIN_DEBUG_DIR)

$(OBJ_DEBUG_DIR):
	mkdir -p $(OBJ_DEBUG_DIR)/nxweb
	mkdir -p $(OBJ_DEBUG_DIR)/modules
	mkdir -p $(OBJ_DEBUG_DIR)/deps/ulib

$(BIN_DEBUG_DIR)/$(BASE_NAME): $(OBJS_DEBUG)
	$(LD) -o $@ $^ $(LDFLAGS_DEBUG)

$(OBJ_DEBUG_DIR)/%.o: %.c $(INC_MAIN) $(INC_MODULES)
	$(CC) -c -o $@ $< $(CFLAGS_DEBUG)

$(BIN_RELEASE_DIR)/$(BASE_NAME): $(OBJS_RELEASE)
	$(LD) -o $@ $^ $(LDFLAGS_RELEASE)

$(OBJ_RELEASE_DIR)/%.o: %.c $(INC_MAIN) $(INC_MODULES)
	$(CC) -c -o $@ $< $(CFLAGS_RELEASE)

SSL_KEYS: ssl/server_cert.pem ssl/server_key.pem ssl/dh.pem

ssl/server_cert.pem: ssl/server_key.pem
	# Generate self-signed certificate for certificate authority, that shall sign other certificates
	$(GNUTLS_BIN_DIR)certtool --generate-privkey --outfile ssl/ca_key.pem
	$(GNUTLS_BIN_DIR)certtool --generate-self-signed --load-privkey ssl/ca_key.pem \
		--template ssl/ca.cfg --outfile ssl/ca_cert.pem
	# Generate certificate using private key
	$(GNUTLS_BIN_DIR)certtool --generate-certificate --load-privkey ssl/server_key.pem \
		--load-ca-certificate ssl/ca_cert.pem --load-ca-privkey ssl/ca_key.pem \
		--template ssl/server.cfg --outfile ssl/server_cert.pem

ssl/server_key.pem:
	# Create private key (RSA by default)
	$(GNUTLS_BIN_DIR)certtool --generate-privkey --outfile ssl/server_key.pem

ssl/dh.pem:
	$(GNUTLS_BIN_DIR)certtool --generate-dh-params --sec-param normal --outfile ssl/dh.pem

clean:
	rm -rf obj/* bin/*

clean_SSL_KEYS:
	rm -f ssl/*.pem

.PHONY: all clean clean_SSL_KEYS Release Debug SSL_KEYS
