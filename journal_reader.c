/* journal_reader.c - Library for dumping the contents of journal directories.
   Copyright (C) 2000,2002 Bruce Guenter

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
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iobuf/iobuf.h>
#include <msg/msg.h>

#include "hash.h"
#include "journal_reader.h"

#if DEBUG_MSGS
#define MSG0(S) do{ printf("%s: %s\n", program, S); }while(0)
#define MSG1(S,A) do{ printf("%s: " S "\n", program, A); }while(0)
#define MSG2(S,A,B) do{ printf("%s: " S "\n", program, A, B); }while(0)
#define MSG3(S,A,B,C) do{ printf("%s: " S "\n", program, A, B, C); }while(0)
#else
#define MSG0(S) do{ }while(0)
#define MSG1(S,A) do{ }while(0)
#define MSG2(S,A,B) do{ }while(0)
#define MSG3(S,A,B,C) do{ }while(0)
#endif

const int msg_show_pid = 0;

static stream* streams;
static unsigned long pagesize;

static unsigned long bytes2ulong(const unsigned char bytes[4])
{
  return (bytes[0]<<24) | (bytes[1]<<16) | (bytes[2]<<8) | bytes[3];
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

static int read_record(unsigned char type, ibuf* in)
{
#undef FAIL
#define FAIL(MSG) do{ error1(MSG); return 0; }while(0)
  static unsigned char header[1+4+4+4];
  static char hcmp[HASH_SIZE];
  static HASH_CTX hash;
  unsigned long strnum;
  unsigned long recnum;
  unsigned long reclen;
  unsigned char* hdrptr;
  char* buf;
  
  if (!ibuf_read(in, header+1, sizeof header-1)) return 0;
  hdrptr = header;
  *hdrptr++ = type;
  strnum = bytes2ulong(hdrptr); hdrptr += 4;
  recnum = bytes2ulong(hdrptr); hdrptr += 4;
  reclen = bytes2ulong(hdrptr);
  buf = alloc_buffer(reclen+HASH_SIZE);
  if (!ibuf_read(in, buf, reclen+HASH_SIZE))
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

static int skip_page(ibuf* in)
{
  unsigned long pos;
  pos = ibuf_tell(in);
  return ibuf_seek(in, pos + (pagesize - pos%pagesize));
}

static int read_transaction(ibuf* in)
{
  unsigned char type;
  if (!ibuf_getc(in, &type)) return 0;
  if (type == 0) return 0;
  do {
    read_record(type, in);
    if (!ibuf_getc(in, &type)) return 0;
  } while (type != 0);
  return skip_page(in);
}

void read_journal(const char* filename)
{
#undef FAIL
#define FAIL(MSG) do{fprintf(stderr,"%s: " MSG ", skipping\n", program, filename);return;}while(0)
  stream* h;
  char header[8+4+4];
  ibuf in;

  if (!ibuf_open(&in, filename, 0)) FAIL("Could not open '%s'");
  if (!ibuf_read(&in, header, sizeof header))
    FAIL("Could not read header from '%s'");
  if (memcmp(header, "journald", 8) != 0)
    FAIL("'%s' is not a journald file");
  if (bytes2ulong(header+8) != 2)
    FAIL("'%s' is not a version 2 journald file");
  if ((pagesize = bytes2ulong(header+12)) == 0)
    FAIL("'%s' has zero page size");
  if (!skip_page(&in)) FAIL("Could not skip first page of '%s'");
  MSG1("Start file '%s'", filename);
  while (read_transaction(&in))
    ;
  ibuf_close(&in);
  MSG1("End file '%s'", filename);
  for (h = streams; h; h = h->next)
    printf("%s: Stream #%lu ID '%s' had no end marker\n",
	   program, h->strnum, h->ident);
}