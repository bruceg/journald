CC = gcc
CFLAGS = -Wall -g

LD = $(CC)
LDFLAGS = $(CFLAGS)
LIBS =

all: journald testclient

journald: journald.o journal_input.o journal_output.o crc32.o
	$(LD) $(LDFLAGS) journald.o journal_input.o journal_output.o crc32.o \
		-o $@ $(LIBS)

testclient: testclient.o journald_client.o
	$(LD) $(LDFLAGS) testclient.o journald_client.o -o $@ $(LIBS)

