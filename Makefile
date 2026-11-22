CC?=cc
CFLAGS?=-O
LIBS=
all: webserver

webserver: webServer.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIBS) webServer.c -o $@

test:
	$(MAKE) -C devel

clean:
	rm -f webserver core webserver.core webserver-static webserver-static.core

.PHONY: all clean

CCARM=arm-linux-gnueabihf-gcc-14
CFLAGSARM= -O2 -Wall -Wall -Wextra  -static
LIBSARM=

INDLUDE_DIRS = -Ilibwebsockets/include -Ilibwebsockets/include/libwebsockets

allarm:
	$(CCARM) $(CFLAGSARM) $(LDFLAGS) $(LIBSARM) webServer.c -o webserver_static
