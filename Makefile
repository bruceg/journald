PACKAGE = journald
VERSION = 0.2

CC = gcc
CFLAGS = -Wall -g -O

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

PROGS = journald journal-read journald-client

all: $(PROGS) journald.a

journald: journald.o journal_input.o journal_output.o md4.o
	$(LD) $(LDFLAGS) journald.o journal_input.o journal_output.o md4.o \
		-o $@ $(LIBS)

journal-read: journal-read.o journal_reader.o md4.o
	$(LD) $(LDFLAGS) journal-read.o journal_reader.o md4.o -o $@ $(LIBS)

journald-client: journald-client.o journald_client.o
	$(LD) $(LDFLAGS) journald-client.o journald_client.o -o $@ $(LIBS)

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
journal-read.o: journal-read.c journal_reader.h
journal_reader.o: journal_reader.c journal_reader.h hash.h
journald-client.o: journald-client.c journald_client.h
md4.o: md4.c md4.h

clean:
	$(RM) *.o *.a $(PROGS)
