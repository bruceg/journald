/* writer-common.c - Common code used by multiple writer routines.
   Copyright (C) 2002 Bruce Guenter

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
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "writer.h"

int (*writer_init)(const char* path);
int (*writer_sync)(void);
int (*writer_seek)(uint32 offset);
int (*writer_writepage)(void);

uint32 writer_pos;
uint32 writer_size;
uint32 writer_pagesize;
unsigned char* writer_pagebuf;

int writer_fd;

extern void writer_fdatasync_select(void);
extern void writer_mmap_select(void);
extern void writer_open_direct_select(void);
extern void writer_open_sync_select(void);

int writer_select(const char* name)
{
  if (strcmp(name, "fdatasync") == 0)
    writer_fdatasync_select();
  else if (strcmp(name, "mmap") == 0)
    writer_mmap_select();
  else if (strcmp(name, "open+direct") == 0)
    writer_open_direct_select();
  else if (strcmp(name, "open+sync") == 0)
    writer_open_sync_select();
  else
    return 0;
  return 1;
}

int writer_open(const char* path, int flags)
{
  struct stat st;
  if ((writer_fd = open(path, O_RDWR|flags)) == -1) return 0;
  if (fstat(writer_fd, &st) == -1) return 0;
  writer_pos = 0;
  if ((writer_pagesize = getpagesize()) < (unsigned)st.st_blksize)
    writer_pagesize = st.st_blksize;
  writer_size = (st.st_size / writer_pagesize) * writer_pagesize;
  return 1;
}

int writer_file_open_flags = 0;

int writer_file_init(const char* path)
{
  if (!writer_open(path, writer_file_open_flags)) return 0;
  if ((writer_pagebuf = mmap(0, writer_pagesize, PROT_READ|PROT_WRITE,
			     MAP_PRIVATE|MAP_ANON, 0, 0)) == 0)
    return 0;
  return 1;
}

int writer_file_seek(uint32 offset)
{
  if ((uint32)lseek(writer_fd, offset, SEEK_SET) != offset)
    return 0;
  writer_pos = offset;
  return 1;
}

int writer_file_writepage(void)
{
  if (writer_pos + writer_pagesize > writer_size)
    return 0;
  if ((uint32)
      write(writer_fd, writer_pagebuf, writer_pagesize) != writer_pagesize)
    return 0;
  writer_pos += writer_pagesize;
  return 1;
}
