#include <sys/types.h>
#define __USE_GNU
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <msg/msg.h>

#include "writer.h"

#ifdef O_DIRECT

static int _sync(void)
{
  return 1;
}

extern int writer_file_open_flags;
extern int writer_file_init(const char* path);
extern int writer_file_seek(unsigned long);
extern int writer_file_writepage(void);

void writer_open_direct_select(void)
{
  writer_file_open_flags = O_DSYNC;
  writer_init = writer_file_init;
  writer_sync = _sync;
  writer_seek = writer_file_seek;
  writer_writepage = writer_file_writepage;
}

#else

void writer_open_direct_select(void)
{
  die1(1, "O_DIRECT not supported on this system");
}

#endif
