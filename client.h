#ifndef JOURNALD__CLIENT__H__
#define JOURNALD__CLIENT__H__

#include <uint32.h>

#define JOURNALD_BUFSIZE 4096
struct journald_client
{
  int fd;
  uint32 bufpos;
  char buf[JOURNALD_BUFSIZE];
};
typedef struct journald_client journald_client;

journald_client* journald_open(const char* path, const char* ident);
int journald_write(journald_client* j, const char* data, uint32 length);
int journald_close(journald_client* j);
int journald_oneshot(const char* path, const char* ident,
		     const char* data, uint32 length);

#endif
