#ifndef JOURNALD__WRITER__H__
#define JOURNALD__WRITER__H__

/* writer-common.c */
extern unsigned long writer_pos;
extern unsigned long writer_size;
extern unsigned char* writer_pagebuf;
extern unsigned writer_pagesize;

extern int writer_fd;

extern int writer_select(const char* name);
extern int writer_open(const char* path, int flags);

/* Assigned by writer_*_select */
extern int (*writer_init)(const char* path);
extern int (*writer_sync)(void);
extern int (*writer_seek)(unsigned long offset);
extern int (*writer_writepage)(void);

#endif
