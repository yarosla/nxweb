CC=gcc
LD=gcc

BASE_NAME=nxweb
LIBS=-lev -lpthread -lcurl

CFLAGS_RELEASE=-pthread -Wno-strict-aliasing -DUSE_CURL -O2 -s
CFLAGS_DEBUG=-pthread -Wno-strict-aliasing -DUSE_CURL -g

LDFLAGS_RELEASE=$(LIBS)
LDFLAGS_DEBUG=$(LIBS)

INC_MAIN=nxweb/nxweb.h nxweb/nxweb_internal.h nxweb/nx_queue.h
SRC_MAIN=nxweb/main.c nxweb/nxweb.c nxweb/http.c nxweb/misc.c nxweb/nx_queue.c

INC_MODULES=
SRC_MODULES=hello.c

# CFLAGS=$(CFLAGS_RELEASE)
# LDFLAGS=$(LDFLAGS_RELEASE)

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

$(BIN_DEBUG_DIR):
	mkdir -p $(BIN_DEBUG_DIR)

$(OBJ_DEBUG_DIR):
	mkdir -p $(OBJ_DEBUG_DIR)/nxweb

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
