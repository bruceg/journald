#include "journald_client.h"

int main(int argc, char* argv[])
{
  int fd;
  int i;
  if (argc < 4) return 1;
  if (argc == 4) return journald_oneshot(argv[1], argv[2],
					 argv[3], strlen(argv[3]));
  fd = journald_open(argv[1], argv[2]);
  if (fd == -1) return 2;
  for (i = 3; i < argc; ++i)
    if (journald_write(fd, argv[i], strlen(argv[i])) == -1) return 3;
  return journald_close(fd);
}
