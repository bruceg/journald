#ifndef JOURNALD__READER__H__
#define JOURNALD__READER__H__

extern const char program[];

extern int reader_argc;
extern char** reader_argv;

struct stream
{
  unsigned long strnum;
  unsigned long recnum;
  unsigned long offset;
  unsigned long start_offset;
  unsigned long identlen;
  char* ident;
  struct stream* next;
  void* data;
};
typedef struct stream stream;

extern void init_stream(stream* s);
extern void append_stream(stream* s, const char* buf, unsigned long reclen);
extern void end_stream(stream* s);
extern void abort_stream(stream* s);

extern void die(const char* msg);
extern void read_journal(const char* filename);

#endif
