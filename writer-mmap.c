#include <string.h>
#include <sys/mman.h>

#include "writer.h"

static uint32 start;
static uint32 end;
static unsigned char* map;

static int _init(const char* path)
{
  if (!writer_open(path, 0)) return 0;
  if ((map = mmap(0, writer_size, PROT_WRITE, MAP_SHARED,
		  writer_fd, 0)) == (unsigned char*)-1)
    return 0;
  writer_pagebuf = map;
  start = end = 0;
  return 1;
}

static int _sync(void)
{
  start = (start / writer_pagesize) * writer_pagesize;
  if (msync(map + start, end-start, MS_SYNC|MS_INVALIDATE) != 0) return 0;
  start = end = writer_pos;
  return 1;
}

static int _seek(uint32 offset)
{
  writer_pos = offset;
  writer_pagebuf = map + writer_pos;
  if (writer_pos + writer_pagesize > end)
    end = writer_pos + writer_pagesize;
  return 1;
}

static int _writepage(void)
{
  if (writer_pos < start)
    start = writer_pos;
  writer_pos += writer_pagesize;
  if (writer_pos > end)
    end = writer_pos;
  writer_pagebuf = map + writer_pos;
  return 1;
}

void writer_mmap_select(void)
{
  writer_init = _init;
  writer_sync = _sync;
  writer_seek = _seek;
  writer_writepage = _writepage;
}
