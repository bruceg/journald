#ifndef JOURNALD__WRITER__H__
#define JOURNALD__WRITER__H__

extern unsigned long writer_pos;
extern unsigned long writer_size;
extern unsigned char* writer_pagebuf;
extern unsigned writer_pagesize;

unsigned long writer_init(const char* path);
int writer_sync(void);
int writer_seek(unsigned long offset);
int writer_writepage(void);

#endif
