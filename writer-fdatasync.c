#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include "writer.h"

static int _init(const char* path)
{
  if (!writer_open(path, 0)) return 0;
  if ((writer_pagebuf = mmap(0, writer_pagesize, PROT_READ|PROT_WRITE,
			     MAP_PRIVATE|MAP_ANONYMOUS, 0, 0)) == 0)
    return 0;
  return 1;
}

static int _sync(void)
{
  return fdatasync(writer_fd) == 0;
}

static int _seek(unsigned long offset)
{
  if ((unsigned long)lseek(writer_fd, offset, SEEK_SET) != offset)
    return 0;
  writer_pos = offset;
  return 1;
}

static int _writepage(void)
{
  if (writer_pos + writer_pagesize > writer_size)
    return 0;
  if ((unsigned long)
      write(writer_fd, writer_pagebuf, writer_pagesize) != writer_pagesize)
    return 0;
  writer_pos += writer_pagesize;
  return 1;
}

void writer_fdatasync_select(void)
{
  writer_init = _init;
  writer_sync = _sync;
  writer_seek = _seek;
  writer_writepage = _writepage;
}
