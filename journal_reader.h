#ifndef JOURNALD__READER__H__
#define JOURNALD__READER__H__

#include <dirent.h>

extern const char* program;

struct dirent_node
{
  struct dirent entry;
  unsigned long use_count;
  struct dirent_node* next;
};
typedef struct dirent_node dirent_node;

struct stream
{
  unsigned long strnum;
  unsigned long offset;
  unsigned long start_offset;
  unsigned long identlen;
  char* ident;
  struct stream* next;
  dirent_node* entry;
  void* data;
};
typedef struct stream stream;

extern void init_stream(stream* s);
extern void append_stream(stream* s, const char* buf, unsigned long reclen);
extern void end_stream(stream* s);
extern void abort_stream(stream* s);

extern void die(const char* msg);
extern void read_journal_directory(const char* dir, int do_unlink);

#endif
