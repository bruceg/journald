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
#include <sys/stat.h>
#include <unistd.h>

#include "writer.h"

int (*writer_init)(const char* path);
int (*writer_sync)(void);
int (*writer_seek)(unsigned long offset);
int (*writer_writepage)(void);

unsigned long writer_pos;
unsigned long writer_size;
unsigned char* writer_pagebuf;
unsigned writer_pagesize;

int writer_fd;

extern void writer_fdatasync_select(void);
extern void writer_mmap_select(void);
extern void writer_open_sync_select(void);

int writer_select(const char* name)
{
  if (strcmp(name, "fdatasync") == 0)
    writer_fdatasync_select();
  else if (strcmp(name, "mmap") == 0)
    writer_mmap_select();
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
