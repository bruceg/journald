#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "journald_client.h"

#ifndef SUN_LEN
/* Evaluate to actual length of the `sockaddr_un' structure.  */
#define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

static int jflush(journald_client* j)
{
  long length;
  char* ptr;
  long wr;
  length = j->bufpos;
  ptr = j->buf;
  while (length > 0) {
    wr = write(j->fd, ptr, length);
    if (wr == -1) return 0;
    length -= wr;
    ptr += wr;
  }
  j->bufpos = 0;
  return 1;
}

static int jwrite(journald_client* j, const char* data, long size)
{
  long length;
  while (size + j->bufpos >= JOURNALD_BUFSIZE) {
    length = JOURNALD_BUFSIZE - j->bufpos;
    memcpy(j->buf+j->bufpos, data, length);
    data += length;
    size -= length;
    j->bufpos += length;
    if (jflush(j) == -1) return 0;
  }
  memcpy(j->buf+j->bufpos, data, size);
  j->bufpos += size;
  return 1;
}

static journald_client* socket_open(const char* path, const char* ident)
{
  size_t size;
  struct sockaddr_un* saddr;
  int fd;
  journald_client* j;
  
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) return 0;
  size = sizeof(struct sockaddr_un) + strlen(path) + 1;
  saddr = (struct sockaddr_un*)malloc(size);
  saddr->sun_family = AF_UNIX;
  strcpy(saddr->sun_path, path);
  if (connect(fd, (struct sockaddr*)saddr, SUN_LEN(saddr)) == -1) return 0;
  free(saddr);

  j = malloc(sizeof(journald_client));
  memset(j, 0, sizeof(journald_client));
  j->fd = fd;
  
  if (!jwrite(j, ident, strlen(ident)+1)) {
    free(j);
    j = 0;
  }
  return j;
}

journald_client* journald_open(const char* path, const char* ident)
{
  journald_client* j;
  
  if (!(j = socket_open(path, ident))) return 0;
  
  if (!jwrite(j, "", 1)) {
    close(j->fd);
    free(j);
    j = 0;
  }
  return j;
}

int journald_write(journald_client* j, const char* data, long length)
{
  char lenstr[30];
  char* ptr;
  long tmp;
  size_t wr;
  size_t total;
  
  ptr = lenstr + 29;
  *ptr-- = 0;
  for (tmp = length; tmp > 0; tmp /= 10)
    *ptr-- = (tmp % 10) + '0';
  ++ptr;
  
  if (!jwrite(j, ptr, lenstr+30-ptr)) return 0;

  for (total = 0; length > 0; total += wr, data += wr, length -= wr)
    if (!jwrite(j, data, length)) return 0;

  return 1;
}

static int read_status_close(journald_client* j)
{
  char tmp[1];
  
  if (!jflush(j)) return 0;
  if (read(j->fd, tmp, 1) != 1) return 0;
  close(j->fd);
  free(j);
  return tmp[0];
}

int journald_close(journald_client* j)
{
  if (!jwrite(j, "", 1)) return 0;
  return read_status_close(j);
}

int journald_oneshot(const char* path, const  char* ident,
		     const char* data, long length)
{
  journald_client* j;

  if (!(j = socket_open(path, ident))) return 0;
  if (!journald_write(j, data, length)) return 0;
  return read_status_close(j);
}
