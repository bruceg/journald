#ifndef JOURNALD__CLIENT__H__
#define JOURNALD__CLIENT__H__

int journald_open(const char* path, const char* ident);
int journald_write(int fd, const char* data, long length);
int journald_close(int fd);
int journald_oneshot(const char* path, const char* ident,
		     const char* data, long length);

#endif
