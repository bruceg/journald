#include <sys/types.h>
#include <unistd.h>

#include "writer.h"

static int _sync(void)
{
  return fdatasync(writer_fd) == 0;
}

extern int writer_file_open_flags;
extern int writer_file_init(const char* path);
extern int writer_file_seek(unsigned long);
extern int writer_file_writepage(void);

void writer_fdatasync_select(void)
{
  writer_file_open_flags = 0;
  writer_init = writer_file_init;
  writer_sync = _sync;
  writer_seek = writer_file_seek;
  writer_writepage = writer_file_writepage;
}
