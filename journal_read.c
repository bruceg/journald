/* journal_read.c - Dump the contents of journal directories.
   Copyright (C) 2000 Bruce Guenter

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include "hash.h"
#include "journald_server.h"

void die(const char* msg)
{
  perror(msg);
  exit(1);
}

static unsigned long bytes2ulong(unsigned char bytes[4])
{
  return (bytes[0]<<24) | (bytes[1]<<16) | (bytes[2]<<8) | bytes[3];
}

struct dirent_node
{
  struct dirent entry;
  struct dirent_node* next;
};
typedef struct dirent_node dirent_node;

static int cmp_dirent(const void* a, const void* b)
{
  const struct dirent* da = (struct dirent*)a;
  const struct dirent* db = (struct dirent*)b;
  return strcmp(da->d_name, db->d_name);
}

static struct dirent* read_directory(void)
{
  DIR* dir;
  struct dirent* de;
  dirent_node* head;
  dirent_node* tail;
  dirent_node* n;
  unsigned count;
  unsigned i;
  
  dir = opendir(".");
  if (!dir) die("opendir");
  head = tail = 0;
  count = 0;
  while ((de = readdir(dir)) != 0) {
    if (de->d_name[0] == '.') continue;
    n = malloc(sizeof(dirent_node));
    n->entry = *de;
    n->next = 0;
    if (tail)
      tail->next = n;
    else
      head = n;
    tail = n;
    ++count;
  }
  closedir(dir);
  de = calloc(count+1, sizeof(struct dirent));
  for (i = 0; head && i < count; i++, head = n) {
    n = head->next;
    de[i] = head->entry;
    free(head);
  }
  de[count].d_name[0] = 0;
  qsort(de, count, sizeof(struct dirent), cmp_dirent);
  return de;
}

static int read_record(FILE* in)
{
  static unsigned char header[1+4+4+4];
  static char ibuf[IDENTSIZE];
  static char rbuf[CBUFSIZE];
  static char hbuf[HASH_SIZE];
  static char hcmp[HASH_SIZE];
  char type;
  unsigned long recnum;
  unsigned long idlen;
  unsigned long reclen;
  HASH_CTX hash;
  
  if (fread(header, sizeof header, 1, in) != 1) return 0;
  if ((type = header[0]) == 0) return 0;
  recnum = bytes2ulong(header+1);
  idlen = bytes2ulong(header+5);
  reclen = bytes2ulong(header+9);
  if (idlen > IDENTSIZE || reclen > CBUFSIZE) return 0;
  if (fread(ibuf, idlen, 1, in) != 1) return 0;
  if (fread(rbuf, reclen, 1, in) != 1) return 0;
  if (fread(hbuf, HASH_SIZE, 1, in) != 1) return 0;
  hash_init(&hash);
  hash_update(&hash, header, sizeof header);
  hash_update(&hash, ibuf, idlen);
  hash_update(&hash, rbuf, reclen);
  hash_finish(&hash, hcmp);
  if (memcmp(hbuf, hcmp, HASH_SIZE)) return 0;
  printf("%c:%lu:%lu,%lu:", type, recnum, idlen, reclen);
  fwrite(ibuf, idlen, 1, stdout);
  fwrite("->", 2, 1, stdout);
  fwrite(rbuf, reclen, 1, stdout);
  putchar('\n');
  return 1;
}

static void read_journal(const char* filename)
{
#define FAIL(MSG) do{fprintf(stderr,"journal_read: " MSG ", skipping\n", filename);return;}while(0)
  static char header[16];
  FILE* in;
  if ((in = fopen(filename, "r")) == 0) FAIL("Could not open '%s'");
  if (fread(header, sizeof header, 1, in) != 1)
    FAIL("Could not read header from '%s'");
  if (memcmp(header, "journald", 8)) FAIL("'%s' is not a journald file");
  if (bytes2ulong(header+8) != 1)
    FAIL("'%s' is not a version 1 journald file");
  while (read_record(in))
    ;
  fclose(in);
}
 
int main(int argc, char* argv[])
{
  struct dirent* de;
  unsigned i;
  if (argc != 2) {
    fprintf(stderr, "usage: %s directory\n", argv[0]);
    return 1;
  }
  if (chdir(argv[1])) die("chdir");
  de = read_directory();
  for (i = 0; de[i].d_name[0]; i++)
    read_journal(de[i].d_name);
  return 0;
}
