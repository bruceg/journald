#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "writer.h"

#ifndef O_DSYNC
#define O_DSYNC O_SYNC
#endif

static int _sync(void)
{
  return 1;
}

extern int writer_file_open_flags;
extern int writer_file_init(const char* path);
extern int writer_file_seek(uint32);
extern int writer_file_writepage(void);

void writer_open_sync_select(void)
{
  writer_file_open_flags = O_DSYNC;
  writer_init = writer_file_init;
  writer_sync = _sync;
  writer_seek = writer_file_seek;
  writer_writepage = writer_file_writepage;
}
