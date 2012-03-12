# Modify as needed
#
LOG_OUTPUT_PATH=busfs.log
REALFS=/tmp/busfs
MOUNTPOINT=$(shell pwd)/mountpoint
#
#
# End of configurables


PATHDEFINES=-DBUSFS_LOGFILE=\"$(LOG_OUTPUT_PATH)\" -DBUSFS_REALFS=\"$(REALFS)\"

CFLAGS=-O0 -pthread -fPIC -ggdb3 -Wall \
	   $(PATHDEFINES) \
	   $(shell pkg-config fuse --cflags) \
	   $(shell pkg-config glib-2.0 --cflags)

LDFLAGS=$(shell pkg-config fuse --libs) \
		$(shell pkg-config glib-2.0 --libs) -lpthread


all: busfs

OBJECTS=busfs.o fops.o boilerplate.o

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $^

busfs: $(OBJECTS) main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	-rm -f $(OBJECTS) busfs

run: busfs
	- fusermount -u $(MOUNTPOINT)
	./busfs -f -o intr -d $(MOUNTPOINT)
