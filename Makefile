CC=gcc
LD=gcc

BASE_NAME=nxweb

###
# Library dependencies:
#  - rt
#  - pthread

###
# nxweb makes use of eventfd:
#   eventfd() is available on Linux since kernel 2.6.22.  Working support is
#   provided in glibc since version 2.8.  The eventfd2() system call
#   is available on Linux since kernel 2.6.27.  Since version 2.9, the glibc
#   eventfd() wrapper will employ the eventfd2() system call, if it is supported
#   by the kernel.

LIBS=-lrt -lpthread

CFLAGS_RELEASE=-pthread -Wno-deprecated-declarations -O2 -s
CFLAGS_DEBUG=-pthread -Wno-deprecated-declarations -g

LDFLAGS_RELEASE=$(LIBS)
LDFLAGS_DEBUG=$(LIBS)

INC_MAIN=nxweb/nxweb.h nxweb/nxweb_internal.h nxweb/config.h nxweb/nx_queue.h nxweb/nx_buffer.h nxweb/nx_pool.h nxweb/nx_event.h nxweb/revision.h
SRC_MAIN=nxweb/main.c nxweb/nxweb2.c nxweb/http.c nxweb/mime.c nxweb/misc.c nxweb/nx_queue.c nxweb/nx_buffer.c nxweb/nx_pool.c nxweb/nx_event.c modules.c

###
# List active modules here; also include them in modules.c file

INC_MODULES=
SRC_MODULES=hello.c sendfile.c benchmark.c modules/upload.c

OBJ_DEBUG_DIR=obj/Debug
BIN_DEBUG_DIR=bin/Debug
OBJ_RELEASE_DIR=obj/Release
BIN_RELEASE_DIR=bin/Release
OBJS_RELEASE=$(patsubst %.c,$(OBJ_RELEASE_DIR)/%.o,$(SRC_MAIN)) $(patsubst %.c,$(OBJ_RELEASE_DIR)/%.o,$(SRC_MODULES))
OBJS_DEBUG=$(patsubst %.c,$(OBJ_DEBUG_DIR)/%.o,$(SRC_MAIN)) $(patsubst %.c,$(OBJ_DEBUG_DIR)/%.o,$(SRC_MODULES))

all: Release

Release: $(BIN_RELEASE_DIR) $(OBJ_RELEASE_DIR) $(BIN_RELEASE_DIR)/$(BASE_NAME)

Debug: $(BIN_DEBUG_DIR) $(OBJ_DEBUG_DIR) $(BIN_DEBUG_DIR)/$(BASE_NAME)

$(BIN_RELEASE_DIR):
	mkdir -p $(BIN_RELEASE_DIR)

$(OBJ_RELEASE_DIR):
	mkdir -p $(OBJ_RELEASE_DIR)/nxweb
	mkdir -p $(OBJ_RELEASE_DIR)/modules

$(BIN_DEBUG_DIR):
	mkdir -p $(BIN_DEBUG_DIR)

$(OBJ_DEBUG_DIR):
	mkdir -p $(OBJ_DEBUG_DIR)/nxweb
	mkdir -p $(OBJ_DEBUG_DIR)/modules

$(BIN_DEBUG_DIR)/$(BASE_NAME): $(OBJS_DEBUG)
	$(LD) -o $@ $^ $(LDFLAGS_DEBUG)

$(OBJ_DEBUG_DIR)/%.o: %.c $(INC_MAIN) $(INC_MODULES)
	$(CC) -c -o $@ $< $(CFLAGS_DEBUG)

$(BIN_RELEASE_DIR)/$(BASE_NAME): $(OBJS_RELEASE)
	$(LD) -o $@ $^ $(LDFLAGS_RELEASE)

$(OBJ_RELEASE_DIR)/%.o: %.c $(INC_MAIN) $(INC_MODULES)
	$(CC) -c -o $@ $< $(CFLAGS_RELEASE)

clean:
	rm -rf obj/* bin/*

.PHONY: all clean Release Debug
