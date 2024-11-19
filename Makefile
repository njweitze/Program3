# Example makefile for CPE 464
#

CC = gcc
CFLAGS = -g -Wall -Werror -Wno-deprecated-declarations
OS = $(shell uname -s)
PROC = $(shell uname -p)
EXEC_SUFFIX=$(OS)-$(PROC)

ifeq ("$(OS)", "SunOS")
	OSLIB=-L/opt/csw/lib -R/opt/csw/lib -lsocket -lnsl
	OSINC=-I/opt/csw/include
	OSDEF=-DSOLARIS
else
ifeq ("$(OS)", "Darwin")
	OSLIB=
	OSINC=
	OSDEF=-DDARWIN
else
	OSLIB=
	OSINC=
	OSDEF=-DLINUX
endif
endif

all:  fishnode-$(EXEC_SUFFIX)

fishnode-$(EXEC_SUFFIX): fishnode.c
	$(CC) $(CFLAGS) $(OSINC) $(OSLIB) $(OSDEF) -o $@ fishnode.c smartalloc.c ./libfish-Linux-x86_64.a

handin: README
	handin bellardo p1 README smartalloc.c smartalloc.h echo.c fish.h fishnode.c Makefile

clean:
	rm -rf fishnode-* fishnode-*.dSYM
