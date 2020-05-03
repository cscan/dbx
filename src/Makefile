#set environment variable RM_INCLUDE_DIR to the location of redismodule.h
ifndef RM_INCLUDE_DIR
	RM_INCLUDE_DIR=../
endif

ifndef RMUTIL_LIBDIR
	RMUTIL_LIBDIR=../rmutil
endif

# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?=  -fno-common -g -ggdb -lc -lm
	SHOBJ_LDFLAGS ?= -shared -Bsymbolic
else ifeq ($(uname_S),Darwin)
	SHOBJ_CFLAGS ?= -dynamic -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup -macosx_version_min 10.13
else
	SHOBJ_CFLAGS ?= -dynamic -fno-common -g -ggdb -lc -lm
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup
endif
CFLAGS = -I$(RM_INCLUDE_DIR) -Wall -g -fPIC -std=gnu99
CC=gcc

all: rmutil dbx.so

rmutil: FORCE
	$(MAKE) -C $(RMUTIL_LIBDIR)

dbx.so: dbx.o
	$(LD) -o $@ dbx.o $(SHOBJ_LDFLAGS) $(LIBS) -L$(RMUTIL_LIBDIR) -lrmutil -lc 

clean:
	rm -rf *.xo *.so *.o

FORCE:
