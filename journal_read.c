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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include "hash.h"
#include "journald_server.h"

void die(const char* msg)
{
  perror(msg);
  exit(1);
}

static unsigned long bytes2ulong(const unsigned char bytes[4])
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

struct handler
{
  unsigned long stream;
  int pipefd;
  pid_t pid;
  struct handler* next;
};
typedef struct handler handler;

static char* ulongtoa(unsigned long i)
{
  static char buf[21];
  char* ptr;
  if (!i) return "0";
  ptr = buf+20;
  *ptr-- = 0;
  while (i) {
    *ptr-- = (i % 10) + '0';
    i /= 10;
  }
  return ptr+1;
}

static char** opt_argv;
static int opt_argc;

static handler* start_handler(unsigned long offset,
			      char* id, unsigned long idlen)
{
  pid_t pid;
  handler* n;
  int pipefds[2];
  
  if (pipe(pipefds)) die("pipe");
  if ((pid = fork()) == -1) die("fork");
  if (!pid) {
    close(pipefds[1]);
    close(0);
    dup2(pipefds[0], 0);
    close(pipefds[0]);
    id[idlen] = 0;
    opt_argv[opt_argc+0] = id;
    opt_argv[opt_argc+1] = ulongtoa(offset);
    opt_argv[opt_argc+2] = 0;
    execvp(opt_argv[0], opt_argv);
    die("exec");
  }
  close(pipefds[0]);
  n = malloc(sizeof(handler));
  n->pid = pid;
  n->pipefd = pipefds[1];
  n->next = 0;
  return n;
}

static handler* handlers;

static handler* find_handler(unsigned long stream, unsigned long offset,
			     const char* id, unsigned long idlen)
{
  handler* ptr;
  for (ptr = handlers; ptr; ptr = ptr->next)
    if (ptr->stream == stream)
      break;
  if (!ptr) {
    ptr = start_handler(offset, (char*)id, idlen);
    ptr->stream = stream;
    ptr->next = handlers;
    handlers = ptr;
  }
  return ptr;
}

static void end_handler(unsigned long stream)
{
  handler* prev;
  handler* ptr;
  int status;
  for (prev = 0, ptr = handlers; ptr; prev = ptr, ptr = ptr->next) {
    if (ptr->stream == stream) {
      if (prev)
	prev->next = ptr->next;
      else
	handlers = ptr->next;
      close(ptr->pipefd);
      waitpid(ptr->pid, &status, WUNTRACED);
      free(ptr);
      return;
    }
  }
}

static int read_record(int in)
{
  static unsigned char header[1+4+4+4+4+4];
  static char buf[IDENTSIZE+CBUFSIZE+HASH_SIZE];
  static char hcmp[HASH_SIZE];
  static HASH_CTX hash;
  char type;
  unsigned long strnum;
  unsigned long recnum;
  unsigned long recoff;
  unsigned long idlen;
  unsigned long reclen;
  const unsigned char* hdrptr;
  handler* h;
  
  if (read(in, header, sizeof header) != sizeof header) return 0;
  hdrptr = header;
  if ((type = *hdrptr++) == 0) return 0;
  strnum = bytes2ulong(hdrptr); hdrptr += 4;
  recnum = bytes2ulong(hdrptr); hdrptr += 4;
  recoff = bytes2ulong(hdrptr); hdrptr += 4;
  idlen = bytes2ulong(hdrptr); hdrptr += 4;
  reclen = bytes2ulong(hdrptr);
  if (idlen > IDENTSIZE || reclen > CBUFSIZE) return 0;
  if (read(in, buf, idlen+reclen+HASH_SIZE) != idlen+reclen+HASH_SIZE)
    return 0;
  hash_init(&hash);
  hash_update(&hash, header, sizeof header);
  hash_update(&hash, buf, idlen+reclen);
  hash_finish(&hash, hcmp);
  if (memcmp(buf+idlen+reclen, hcmp, HASH_SIZE)) return 0;
  h = find_handler(strnum, recoff, buf, idlen);
  if (write(h->pipefd, buf+idlen, reclen) != reclen) die("write to child");
  if (type == 'O' || type == 'E') end_handler(strnum);
  return 1;
}

static void read_journal(const char* filename)
{
#undef FAIL
#define FAIL(MSG) do{fprintf(stderr,"journal_read: " MSG ", skipping\n", filename);return;}while(0)
  static char header[16];
  int in;
  if ((in = open(filename, O_RDONLY)) == -1) FAIL("Could not open '%s'");
  if (read(in, header, sizeof header) != sizeof header)
    FAIL("Could not read header from '%s'");
  if (memcmp(header, "journald", 8)) FAIL("'%s' is not a journald file");
  if (bytes2ulong(header+8) != 0)
    FAIL("'%s' is not a version 0 journald file");
  printf("File:%s\n", filename);
  while (read_record(in))
    ;
  close(in);
}

int main(int argc, char* argv[])
{
  struct dirent* de;
  unsigned i;
  if (argc < 3) {
    fprintf(stderr, "usage: %s directory program [args ...]\n", argv[0]);
    return 1;
  }
  if (chdir(argv[1])) die("chdir");
  opt_argc = argc - 2;
  opt_argv = malloc(sizeof(char*) * (opt_argc+3));
  for (i = 0; i < opt_argc; i++)
    opt_argv[i] = argv[i+2];
  de = read_directory();
  for (i = 0; de[i].d_name[0]; i++)
    read_journal(de[i].d_name);
  return 0;
}
