#include "conf_bin.c"
#include <installer.h>

void insthier(void) {
  int bin = opendir(conf_bin);
  c(bin, "journald-client",     -1, -1, 0755);
  c(bin, "journald-fdatasync",  -1, -1, 0755);
  c(bin, "journald-mmap",       -1, -1, 0755);
  c(bin, "journald-open-dsync", -1, -1, 0755);
  c(bin, "journal-dump",        -1, -1, 0755);
  c(bin, "journal-read",        -1, -1, 0755);
}
