#ifndef JOURNALD__SERVER__H__
#define JOURNALD__SERVER__H__

#include <uint32.h>

#define IDENTSIZE 1024
#define CBUFSIZE 8192

struct connection 
{
  int fd;
  int mode;
  int state;
  uint32 count;
  uint32 length;
  uint32 ident_len;
  uint32 buf_length;
  int wrote_ident;
  int ok;
  uint32 total;
  uint32 records;
  uint32 number;
  
  char ident[IDENTSIZE];
  char buf[CBUFSIZE];
};
typedef struct connection connection;

extern connection* connections;
extern unsigned opt_connections;

extern void die(const char* msg);
extern void handle_data(connection* con, char* data, uint32 size);
extern int open_journal(const char* filename);
extern int write_record(connection* con, int final, int do_abort);
extern int sync_records(void);
extern int rotate_journal(void);

#endif
