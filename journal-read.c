/* journal-read.c - Dump the contents of journal directories.
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

static const char* program = "journal-read";

#define MSG0(S) do{ printf("%s: %s\n", program, S); }while(0)
#define MSG1(S,A) do{ printf("%s: " S "\n", program, A); }while(0)
#define MSG2(S,A,B) do{ printf("%s: " S "\n", program, A, B); }while(0)
#define MSG3(S,A,B,C) do{ printf("%s: " S "\n", program, A, B, C); }while(0)

struct dirent_node
{
  struct dirent entry;
  unsigned long use_count;
  struct dirent_node* next;
};
typedef struct dirent_node dirent_node;

struct handler
{
  unsigned long stream;
  unsigned long offset;
  unsigned long start_offset;
  int fd;
  unsigned long identlen;
  char* ident;
  struct handler* next;
  dirent_node* entry;
};
typedef struct handler handler;

static char** opt_argv;
static int opt_argc;
static int opt_unlink;

static handler* handlers;

static dirent_node* direntries;
static dirent_node* curr_entry;

static void die(const char* msg)
{
  perror(msg);
  exit(1);
}

static unsigned long bytes2ulong(const unsigned char bytes[4])
{
  return (bytes[0]<<24) | (bytes[1]<<16) | (bytes[2]<<8) | bytes[3];
}

static int cmp_dirent_node(const void* a, const void* b)
{
  const dirent_node* da = *(dirent_node**)a;
  const dirent_node* db = *(dirent_node**)b;
  return strcmp(da->entry.d_name, db->entry.d_name);
}

static void read_directory(void)
{
  DIR* dir;
  struct dirent* de;
  dirent_node* n;
  dirent_node* list;
  dirent_node** tmp;
  unsigned count;
  unsigned i;
  
  dir = opendir(".");
  if (!dir) die("opendir");
  list = 0;
  count = 0;
  while ((de = readdir(dir)) != 0) {
    if (de->d_name[0] == '.') continue;
    n = malloc(sizeof(dirent_node));
    n->entry = *de;
    n->use_count = 0;
    n->next = list;
    list = n;
    ++count;
  }
  closedir(dir);

  tmp = calloc(count, sizeof(dirent_node*));
  for (i = 0; list && i < count; i++, list = list->next)
    tmp[i] = list;
  qsort(tmp, count, sizeof(dirent_node*), cmp_dirent_node);
  direntries = tmp[0];
  for (i = 0; i < count-1; i++)
    tmp[i]->next = tmp[i+1];
  tmp[i]->next = 0;
  free(tmp);
}

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

static void exec_handler(handler* h)
{
  pid_t pid;
  
  if (lseek(h->fd, 0, SEEK_SET) != 0) die("lseek");
  if ((pid = fork()) == -1) die("fork");
  if (!pid) {
    close(0);
    dup2(h->fd, 0);
    close(h->fd);
    opt_argv[opt_argc+0] = h->ident;
    opt_argv[opt_argc+1] = ulongtoa(h->start_offset);
    opt_argv[opt_argc+2] = 0;
    execvp(opt_argv[0], opt_argv);
    die("exec");
  }
  if (waitpid(pid, 0, WUNTRACED) != pid) die("waitpid");
}

static handler* add_handler(unsigned long stream, unsigned long offset,
			    char* id, unsigned long idlen)
{
  char filename[] = "journal-read.tmp.XXXXXX";
  handler* n;
  n = malloc(sizeof(handler));
  n->stream = stream;
  n->offset = n->start_offset = offset;
  if ((n->fd = mkstemp(filename)) == -1) die("mkstemp");
  if (unlink(filename)) die("unlink");
  n->identlen = idlen;
  n->ident = malloc(idlen+1);
  memcpy(n->ident, id, idlen);
  n->ident[idlen] = 0;
  n->next = handlers;
  n->entry = curr_entry;
  n->entry->use_count++;
  handlers = n;
  return n;
}

static handler* find_handler(unsigned long stream)
{
  handler* ptr;
  for (ptr = handlers; ptr; ptr = ptr->next)
    if (ptr->stream == stream)
      break;
  return ptr;
}

static void del_handler(handler* find)
{
  handler* prev;
  handler* ptr;
  for (prev = 0, ptr = handlers; ptr; prev = ptr, ptr = ptr->next) {
    if (ptr == find) {
      if (prev)
	prev->next = ptr->next;
      else
	handlers = ptr->next;
      close(ptr->fd);
      ptr->entry->use_count--;
      if (opt_unlink) {
	while (!direntries->use_count && direntries != curr_entry) {
	  MSG1("Removing '%s'...", direntries->entry.d_name);
	  unlink(direntries->entry.d_name);
	  direntries = direntries->next;
	}
      }
      free(ptr);
      return;
    }
  }
}

static char* alloc_buffer(unsigned long size)
{
  static char* buf = 0;
  static unsigned long prev_size = 0;
  if (size >= prev_size) {
    if (buf) free(buf);
    buf = malloc(size);
    prev_size = size;
  }
  return buf;
}

static void handle_record(char type, unsigned long strnum,
			  unsigned long reclen, const char* buf)
{
  handler* h;
  unsigned long offset;
  
  h = find_handler(strnum);
  if (type == 'A') {
    if (h)
      del_handler(h);
  }
  else if (type == 'I') {
    offset = bytes2ulong(buf);
    if (h) {
      if (offset != h->offset) {
	MSG3("Bad offset value for stream #%lu, dropping stream\n"
	     "  Offset was %lu, should be %lu\n", h->stream,
	     offset, h->offset);
	del_handler(h);
      }
    }
    else {
      MSG2("Start stream #%lu at offset %lu", strnum, offset);
      add_handler(strnum, offset, (char*)buf+4, reclen-4);
    }
  }
  else {
    if (type == 'O' || type == 'D' || type == 'E') {
      if (h) {
	if (write(h->fd, buf, reclen) != reclen)
	  die("write to temporary file");
	h->offset += reclen;
      }
      else
	MSG1("Data record for nonexistant stream #%lu", strnum);
    }
    if (type == 'O' || type == 'E') {
      if (h) {
	MSG2("End stream #%lu at offset %lu", strnum, h->offset);
	exec_handler(h);
	del_handler(h);
      }
      else
	MSG1("End record for nonexistant stream #%lu\n", strnum);
    }
  }
}

static int read_record(int in)
{
#undef FAIL
#define FAIL(ARGS) do{ printf ARGS; return 0; }while(0)
  static unsigned char header[1+4+4+4];
  static char hcmp[HASH_SIZE];
  static HASH_CTX hash;
  char type;
  unsigned long strnum;
  unsigned long recnum;
  unsigned long reclen;
  const unsigned char* hdrptr;
  char* buf;
  
  if (read(in, header, sizeof header) != sizeof header) return 0;
  hdrptr = header;
  if ((type = *hdrptr++) == 0) return 0;
  strnum = bytes2ulong(hdrptr); hdrptr += 4;
  recnum = bytes2ulong(hdrptr); hdrptr += 4;
  reclen = bytes2ulong(hdrptr);
  buf = alloc_buffer(reclen+HASH_SIZE);
  if (read(in, buf, reclen+HASH_SIZE) != reclen+HASH_SIZE)
    FAIL(("journal-read: Could not read record data.\n"));
  hash_init(&hash);
  hash_update(&hash, header, sizeof header);
  hash_update(&hash, buf, reclen);
  hash_finish(&hash, hcmp);
  if (memcmp(buf+reclen, hcmp, HASH_SIZE))
    FAIL(("journal-read: Record data was corrupted.\n"));

  handle_record(type, strnum, reclen, buf);
  return 1;
}

static void read_journal()
{
#undef FAIL
#define FAIL(MSG) do{fprintf(stderr,"journal-read: " MSG ", skipping\n", filename);return;}while(0)
  static char header[16];
  int in;
  const char* filename;

  filename = curr_entry->entry.d_name;
  if ((in = open(filename, O_RDONLY)) == -1) FAIL("Could not open '%s'");
  if (read(in, header, sizeof header) != sizeof header)
    FAIL("Could not read header from '%s'");
  if (memcmp(header, "journald", 8)) FAIL("'%s' is not a journald file");
  if (bytes2ulong(header+8) != 1)
    FAIL("'%s' is not a version 1 journald file");
  MSG1("Start file '%s'", filename);
  while (read_record(in))
    ;
  close(in);
  MSG1("End file '%s'", filename);
}

static void usage(const char* msg)
{
  if (msg)
    fprintf(stderr, "%s: %s\n", "journal-read", msg);
  fputs("usage: journal-read [-u] directory program [args ...]\n", stderr);
  exit(1);
}

static void parse_args(int argc, char* argv[])
{
  int opt;
  int i;
  while ((opt = getopt(argc, argv, "u")) != EOF) {
    switch (opt) {
    case 'u': opt_unlink = 1; break;
    default: usage(0);
    }
  }
  argc -= optind;
  argv += optind;
  if (argc < 2) usage("Too few command-line arguments");
  if (chdir(argv[0])) die("chdir");
  opt_argc = argc - 1;
  opt_argv = malloc(sizeof(char*) * (opt_argc+3));
  for (i = 0; i < opt_argc; i++)
    opt_argv[i] = argv[i+1];
}

int main(int argc, char* argv[])
{
  handler* h;
  parse_args(argc, argv);
  read_directory();
  for (curr_entry = direntries; curr_entry; curr_entry = curr_entry->next)
    read_journal();
  for (h = handlers; h; h = h->next)
    printf("journal-read: Stream #%lu ID '%s' had no end marker\n",
	   h->stream, h->ident);
  return 0;
}
