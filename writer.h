#ifndef JOURNALD__WRITER__H__
#define JOURNALD__WRITER__H__

#include <uint32.h>

/* writer-common.c */
extern uint32 writer_pos;
extern uint32 writer_size;
extern uint32 writer_pagesize;
extern unsigned char* writer_pagebuf;

extern int writer_fd;

extern int writer_select(const char* name);
extern int writer_open(const char* path, int flags);

/* Assigned by writer_*_select */
extern int (*writer_init)(const char* path);
extern int (*writer_sync)(void);
extern int (*writer_seek)(uint32 offset);
extern int (*writer_writepage)(void);

#endif
