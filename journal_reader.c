/* journal_reader.c - Library for dumping the contents of journal directories.
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

extern const char* program;

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

struct stream
{
  unsigned long strnum;
  unsigned long offset;
  unsigned long start_offset;
  unsigned long identlen;
  char* ident;
  struct stream* next;
  dirent_node* entry;
  void* data;
};
typedef struct stream stream;

extern void init_stream(stream* s);
extern void append_stream(stream* s, const char* buf, unsigned long reclen);
extern void end_stream(stream* s);
extern void abort_stream(stream* s);

static int opt_unlink;

static stream* streams;

static dirent_node* direntries;
static dirent_node* curr_entry;

void die(const char* msg)
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

static stream* new_stream(unsigned long strnum, unsigned long offset,
			  char* id, unsigned long idlen)
{
  stream* n;
  n = malloc(sizeof(stream));
  n->strnum = strnum;
  n->offset = n->start_offset = offset;
  n->identlen = idlen;
  n->ident = malloc(idlen+1);
  memcpy(n->ident, id, idlen);
  n->ident[idlen] = 0;
  n->next = streams;
  n->entry = curr_entry;
  n->entry->use_count++;
  streams = n;
  init_stream(n);
  return n;
}

static stream* find_stream(unsigned long strnum)
{
  stream* ptr;
  for (ptr = streams; ptr; ptr = ptr->next)
    if (ptr->strnum == strnum)
      break;
  return ptr;
}

static void del_stream(stream* find)
{
  stream* prev;
  stream* ptr;
  for (prev = 0, ptr = streams; ptr; prev = ptr, ptr = ptr->next) {
    if (ptr == find) {
      if (prev)
	prev->next = ptr->next;
      else
	streams = ptr->next;
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
  stream* h;
  unsigned long offset;
  
  h = find_stream(strnum);
  if (type == 'A') {
    if (h) {
      abort_stream(h);
      del_stream(h);
    }
  }
  else if (type == 'I') {
    offset = bytes2ulong(buf);
    if (h) {
      if (offset != h->offset) {
	MSG3("Bad offset value for stream #%lu, dropping stream\n"
	     "  Offset was %lu, should be %lu\n", h->strnum,
	     offset, h->offset);
	abort_stream(h);
	del_stream(h);
      }
    }
    else {
      MSG2("Start stream #%lu at offset %lu", strnum, offset);
      new_stream(strnum, offset, (char*)buf+4, reclen-4);
    }
  }
  else {
    if (type == 'O' || type == 'D' || type == 'E') {
      if (h) {
	append_stream(h, buf, reclen);
	h->offset += reclen;
      }
      else
	MSG1("Data record for nonexistant stream #%lu", strnum);
    }
    if (type == 'O' || type == 'E') {
      if (h) {
	MSG2("End stream #%lu at offset %lu", strnum, h->offset);
	end_stream(h);
	del_stream(h);
      }
      else
	MSG1("End record for nonexistant stream #%lu\n", strnum);
    }
  }
}

static int read_record(int in)
{
#undef FAIL
#define FAIL(MSG) do{ printf("%s: %s\n", program, MSG); return 0; }while(0)
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
    FAIL("Could not read record data.");
  hash_init(&hash);
  hash_update(&hash, header, sizeof header);
  hash_update(&hash, buf, reclen);
  hash_finish(&hash, hcmp);
  if (memcmp(buf+reclen, hcmp, HASH_SIZE))
    FAIL("Record data was corrupted.");

  handle_record(type, strnum, reclen, buf);
  return 1;
}

static void read_journal()
{
#undef FAIL
#define FAIL(MSG) do{fprintf(stderr,"%s: " MSG ", skipping\n", program, filename);return;}while(0)
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

void read_journal_directory(const char* dir, int do_unlink)
{
  stream* h;
  if (dir && chdir(dir))
    die("Could not change directory");
  opt_unlink = do_unlink;
  read_directory();
  for (curr_entry = direntries; curr_entry; curr_entry = curr_entry->next)
    read_journal();
  for (h = streams; h; h = h->next)
    printf("%s: Stream #%lu ID '%s' had no end marker\n",
	   program, h->strnum, h->ident);
}
