#ifndef JOURNALD__SERVER__H__
#define JOURNALD__SERVER__H__

#define IDENTSIZE 1024
#define CBUFSIZE 8192

struct connection 
{
  int fd;
  int mode;
  int state;
  int count;
  unsigned long length;
  unsigned long ident_len;
  unsigned long buf_length;
  int wrote_ident;
  int ok;
  unsigned long total;
  unsigned long records;
  unsigned long number;
  
  char ident[IDENTSIZE];
  char buf[CBUFSIZE];
};
typedef struct connection connection;

extern connection* connections;
extern unsigned opt_connections;
extern int opt_twopass;
extern unsigned long opt_maxsize;

extern void die(const char* msg);
extern void handle_data(connection* con, char* data, unsigned long size);
extern int open_journal(void);
extern int write_record(connection* con, int final, int abort);
extern int sync_records(void);
extern int rotate_journal(void);

#endif
