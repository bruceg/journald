#include "journald_client.h"

int main(int argc, char* argv[])
{
  int fd;
  int i;
  if (argc < 3) return 1;
  fd = journald_open("/tmp/.journald", argv[1]);
  for (i = 2; i < argc; ++i)
    journald_write(fd, argv[i], strlen(argv[i]));
  return journald_close(fd);
}
