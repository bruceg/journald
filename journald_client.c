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

static int socket_open(const char* path, const char* ident)
{
  size_t size;
  struct sockaddr_un* saddr;
  int fd;
  size_t wr;
  
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) return -1;
  size = sizeof(struct sockaddr_un) + strlen(path) + 1;
  saddr = (struct sockaddr_un*)malloc(size);
  saddr->sun_family = AF_UNIX;
  strcpy(saddr->sun_path, path);
  if (connect(fd, (struct sockaddr*)saddr, SUN_LEN(saddr)) == -1) return -1;
  free(saddr);

  size = strlen(ident) + 1;
  while (size) {
    wr = write(fd, ident, size);
    if (!wr || wr == -1) {
      close(fd);
      return -1;
    }
    size -= wr;
    ident += wr;
  }

  return fd;
}

int journald_open(const char* path, const char* ident)
{
  int fd;
  
  if ((fd = socket_open(path, ident)) == -1) return -1;
  
  if (write(fd, "", 1) != 1) {
    close(fd);
    return -1;
  }
  return fd;
}

int journald_write(int fd, const char* data, long length)
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
  
  if (write(fd, ptr, lenstr+30-ptr) != lenstr+30-ptr) return -1;

  for (total = 0; length > 0; total += wr, data += wr, length -= wr) {
    wr = write(fd, data, length);
    if (!wr || wr == -1) return -1;
  }
  return total;
}

static int read_status_close(int fd)
{
  char tmp[1];
  
  if (read(fd, tmp, 1) != 1) return -1;
  close(fd);
  return tmp[0] ? 0 : -1;
}

int journald_close(int fd)
{
  if (write(fd, "", 1) != 1) return -1;
  return read_status_close(fd);
}

int journald_oneshot(const char* path, const  char* ident,
		     const char* data, long length)
{
  int fd;

  if ((fd = socket_open(path, ident)) == -1) return -1;
  if (journald_write(fd, data, length) != length) return -1;
  return read_status_close(fd);
}
