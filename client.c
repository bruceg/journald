/* journald_client.c - Journald client library.
   Copyright (C) 2000,2002 Bruce Guenter

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "client.h"

#ifndef SUN_LEN
/* Evaluate to actual length of the `sockaddr_un' structure.  */
#define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

static int jflush(journald_client* j)
{
  unsigned long length;
  char* ptr;
  unsigned long wr;
  length = j->bufpos;
  ptr = j->buf;
  while (length > 0) {
    wr = write(j->fd, ptr, length);
    if (wr == (unsigned long)-1) return 0;
    length -= wr;
    ptr += wr;
  }
  j->bufpos = 0;
  return 1;
}

static int jwrite(journald_client* j, const char* data, unsigned long size)
{
  unsigned long length;
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

int journald_write(journald_client* j, const char* data, unsigned long size)
{
  char buf[4];
  unsigned long v = size;
  
  buf[3] = v & 0xff; v >>= 8;
  buf[2] = v & 0xff; v >>= 8;
  buf[1] = v & 0xff; v >>= 8;
  buf[0] = v;
  
  return jwrite(j, buf, 4) && jwrite(j, data, size);
}

journald_client* journald_open(const char* path, const char* ident)
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
  
  if (!journald_write(j, ident, strlen(ident))) {
    free(j);
    j = 0;
  }
  return j;
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
  if (!journald_write(j, 0, 0)) return 0;
  return read_status_close(j);
}

int journald_oneshot(const char* path, const  char* ident,
		     const char* data, unsigned long length)
{
  journald_client* j;

  if (!(j = journald_open(path, ident))) return 0;
  if (!journald_write(j, data, length)) return 0;
  return read_status_close(j);
}
