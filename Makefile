FUSE_CFLAGS=$(shell pkg-config fuse --cflags)
FUSE_LIBS=$(shell pkg-config fuse --libs)

LCB_ROOT:= /opt/couchbase

CFLAGS=-I$(LCB_ROOT)/include $(FUSE_CFLAGS)
LIBS=-L$(LCB_ROOT)/lib -Wl,-rpath,$(LCB_ROOT)/lib $(FUSE_LIBS) -lcouchbase

all: mount_cbfs

mount_cbfs: mount_cbfs.o cJSON.o cJSON.h
	$(LINK.c) -o mount_cbfs mount_cbfs.o cJSON.o $(LIBS)

mount_cbfs.o: mount_cbfs.c cJSON.h
	$(COMPILE.c) mount_cbfs.c

cJSON.o: cJSON.c cJSON.h
	$(COMPILE.c) cJSON.c

clean:
	$(RM) mount_cbfs mount_cbfs.o cJSON.o
