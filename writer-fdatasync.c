#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "writer.h"

static int fd;
unsigned long writer_size;
unsigned long writer_pos;
unsigned writer_pagesize;
unsigned char* writer_pagebuf;

unsigned long writer_init(const char* path)
{
  struct stat st;
  if ((fd = open(path, O_WRONLY)) == -1) return 0;
  if (fstat(fd, &st) == -1) return 0;
  writer_pos = 0;
  if ((writer_pagesize = getpagesize()) < st.st_blksize)
    writer_pagesize = st.st_blksize;
  writer_size = (st.st_size / writer_pagesize) * writer_pagesize;
  if ((writer_pagebuf = mmap(0, writer_pagesize, PROT_READ|PROT_WRITE,
			     MAP_PRIVATE|MAP_ANONYMOUS, 0, 0)) == 0)
    return 0;
  return writer_size;
}

int writer_sync(void)
{
  return fdatasync(fd) == 0;
}

int writer_seek(unsigned long offset)
{
  if (lseek(fd, offset, SEEK_SET) != offset)
    return 0;
  writer_pos = offset;
  return 1;
}

int writer_writepage(void)
{
  if (writer_pos + writer_pagesize > writer_size)
    return 0;
  if (write(fd, writer_pagebuf, writer_pagesize) != writer_pagesize)
    return 0;
  writer_pos += writer_pagesize;
  return 1;
}
