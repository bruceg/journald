#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "writer.h"

static int fd;
static unsigned long start;
static unsigned long end;
static unsigned char* map;
unsigned writer_pagesize;
unsigned long writer_size;
unsigned long writer_pos;
unsigned char* writer_pagebuf;

unsigned long writer_init(const char* path)
{
  struct stat st;
  if ((fd = open(path, O_RDWR)) == -1) return 0;
  if (fstat(fd, &st) == -1) return 0;
  /* Truncate the size down to an even multiple of the page size */
  if ((writer_pagesize = getpagesize()) < st.st_blksize)
    writer_pagesize = st.st_blksize;
  writer_size = (st.st_size / writer_pagesize) * writer_pagesize;
  if ((map = mmap(0, writer_size, PROT_WRITE, MAP_SHARED, fd, 0)) == (char*)-1)
    return 0;
  writer_pos = 0;
  writer_pagebuf = map;
  start = end = 0;
  return writer_size;
}

int writer_sync(void)
{
  start = (start / writer_pagesize) * writer_pagesize;
  if (msync(map + start, end-start, MS_SYNC|MS_INVALIDATE) != 0) return 0;
  start = end = writer_pos;
  return 1;
}

int writer_seek(unsigned long offset)
{
  writer_pos = offset;
  writer_pagebuf = map + writer_pos;
  if (writer_pos + writer_pagesize > end)
    end = writer_pos + writer_pagesize;
  return 1;
}

int writer_writepage(void)
{
  if (writer_pos < start)
    start = writer_pos;
  writer_pos += writer_pagesize;
  if (writer_pos > end)
    end = writer_pos;
  writer_pagebuf = map + writer_pos;
  return 1;
}
