#ifndef JOURNALD__READER__H__
#define JOURNALD__READER__H__

#include <uint32.h>

#define DEBUG_JOURNAL 1

extern const char program[];

extern int reader_argc;
extern char** reader_argv;

struct stream
{
  uint32 strnum;
  uint32 recnum;
  uint32 offset;
  uint32 start_offset;
  uint32 identlen;
  char* ident;
  struct stream* next;
  void* data;
};
typedef struct stream stream;

extern void init_stream(stream* s);
extern void append_stream(stream* s, const char* buf, uint32 reclen);
extern void end_stream(stream* s);
extern void abort_stream(stream* s);

extern void die(const char* msg);
extern void read_journal(const char* filename);

#endif
