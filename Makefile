CC = gcc
CFLAGS = -Wall -g

LD = $(CC)
LDFLAGS = $(CFLAGS)
LIBS =

all: journald testclient

journald: journald.o journal_input.o journal_output.o md4.o
	$(LD) $(LDFLAGS) journald.o journal_input.o journal_output.o md4.o \
		-o $@ $(LIBS)

testclient: testclient.o journald_client.o
	$(LD) $(LDFLAGS) testclient.o journald_client.o -o $@ $(LIBS)

journald.o: journald.c journald.h
journal_input.o: journal_input.c journald.h
journal_output.o: journal_output.c journald.h md4.h
journald_client.o: journald_client.c journald_client.h
testclient.o: testclient.c journald_client.h
md4.o: md4.c md4.h
