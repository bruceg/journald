PACKAGE = journald
VERSION = 0.1

CC = gcc
CFLAGS = -Wall -g

LD = $(CC)
LDFLAGS = $(CFLAGS)
LIBS =

AR = ar
RANLIB = ranlib

install_prefix =
prefix = /usr/local
bindir = $(prefix)/bin
mandir = $(prefix)/man
man1dir = $(mandir)/man1
libdir = $(prefix)/lib

install = install
installbin = $(install) -m 555
installdir = $(install) -d
installsrc = $(install) -m 444

PROGS = journald journal_read # testclient

all: $(PROGS) journald.a

journald: journald.o journal_input.o journal_output.o md4.o
	$(LD) $(LDFLAGS) journald.o journal_input.o journal_output.o md4.o \
		-o $@ $(LIBS)

journal_read: journal_read.o md4.o
	$(LD) $(LDFLAGS) journal_read.o md4.o -o $@ $(LIBS)

testclient: testclient.o journald_client.o
	$(LD) $(LDFLAGS) testclient.o journald_client.o -o $@ $(LIBS)

install: all
	$(installdir) $(install_prefix)$(bindir)
	$(installbin) $(PROGS) $(install_prefix)$(bindir)

	$(installdir) $(install_prefix)$(libdir)
	$(installsrc) journald.a $(install_prefix)$(libdir)

journald.a: journald_client.o
	$(AR) rc $@ journald_client.o
	$(RANLIB) $@

journald.o: journald.c journald_server.h
journal_input.o: journal_input.c journald_server.h
journal_output.o: journal_output.c journald_server.h hash.h
journald_client.o: journald_client.c journald_client.h
journal_read.o: journal_read.c journald_server.h hash.h
testclient.o: testclient.c journald_client.h
md4.o: md4.c md4.h

clean:
	$(RM) *.o *.a $(PROGS)
