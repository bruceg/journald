#include <unistd.h>
#include "journald_client.h"

int main(int argc, char* argv[])
{
  journald_client* j;
  char buf[4096];
  long rd;

  if (argc != 3) return 1;
  j = journald_open(argv[1], argv[2]);
  if (!j) return 2;
  for(;;) {
    rd = read(0, buf, sizeof buf);
    if (rd == -1) return 3;
    if (!rd) break;
    if (!journald_write(j, buf, rd)) return 4;
  }
  if (!journald_close(j)) return 5;
  return 0;
}
