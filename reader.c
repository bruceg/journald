/* reader.c - Library for dumping the contents of journal directories.
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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cli/cli.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <str/str.h>

#include "flags.h"
#include "hash.h"
#include "reader.h"

const int msg_show_pid = 0;

int reader_argc;
char** reader_argv;

static stream* streams;
static unsigned long pagesize;
static unsigned long global_recnum;

static unsigned long bytes2ulong(const unsigned char bytes[4])
{
  return (bytes[0]<<24) | (bytes[1]<<16) | (bytes[2]<<8) | bytes[3];
}

static stream* new_stream(unsigned long strnum, unsigned long recnum,
			  unsigned long offset, char* id, unsigned long idlen)
{
  stream* n;
  n = malloc(sizeof(stream));
  n->strnum = strnum;
  n->recnum = recnum;
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
      free(ptr->ident);
      free(ptr);
      return;
    }
  }
}

void str_copyu(str* s, unsigned long u)
{
  str_truncate(s, 0) && str_catu(s, u);
}

static void handle_record(unsigned long typeflags, unsigned long strnum,
			  unsigned long recnum, unsigned long reclen,
			  const char* buf)
{
  stream* h;
  unsigned long offset;
  static str srecnum;
  static str sstrnum;
  static str soffset;

  str_copyu(&srecnum, recnum);
  str_copyu(&sstrnum, strnum);
  if ((h = find_stream(strnum)) != 0) {
    str_copyu(&soffset, h->offset);
    if (recnum != h->recnum) {
      warn3("Bad record number for stream #", sstrnum.s, ", dropping stream");
      abort_stream(h);
      del_stream(h);
      return;
    }
  }
  if (typeflags & RECORD_ABORT) {
    if (h) {
      debug6(DEBUG_JOURNAL, "Abort stream #", sstrnum.s,
	     " at record ", srecnum.s, " offset ", soffset.s);
      abort_stream(h);
      del_stream(h);
    }
  }
  else if (typeflags & RECORD_INFO) {
    offset = bytes2ulong(buf);
    if (h)
      warn3("Info record for existing stream #", sstrnum.s, ", ignoring");
    else {
      debug6(DEBUG_JOURNAL, "Start stream #", sstrnum.s,
	     " at record ", srecnum.s, " offset ", soffset.s);
      new_stream(strnum, recnum, offset, (char*)buf+4, reclen-4);
    }
  }
  else {
    if (typeflags & RECORD_DATA) {
      if (h) {
	debug6(DEBUG_JOURNAL, "Append stream #", sstrnum.s,
	       " record ", srecnum.s, " offset ", soffset.s);
	append_stream(h, buf, reclen);
	h->offset += reclen;
	h->recnum ++;
      }
      else
	warn2("Data record for nonexistant stream #", sstrnum.s);
    }
    if (typeflags & RECORD_EOS) {
      if (h) {
	debug6(DEBUG_JOURNAL, "End stream #", sstrnum.s,
	       " at record ", srecnum.s, " offset ", soffset.s);
	end_stream(h);
	del_stream(h);
      }
      else
	warn2("End record for nonexistant stream #", sstrnum.s);
    }
  }
}

static int read_record(unsigned char header[HEADER_SIZE], ibuf* in)
{
  static char hcmp[HASH_SIZE];
  static HASH_CTX hash;
  unsigned long strnum;
  unsigned long recnum;
  unsigned long grecnum;
  unsigned long reclen;
  unsigned long typeflags;
  unsigned char* hdrptr;
  static str buf;

  hdrptr = header;
  typeflags = bytes2ulong(hdrptr); hdrptr += 4;
  grecnum = bytes2ulong(hdrptr); hdrptr += 4;
  if (grecnum != global_recnum)
    die1(1, "Global record number mismatch.");
  strnum = bytes2ulong(hdrptr); hdrptr += 4;
  recnum = bytes2ulong(hdrptr); hdrptr += 4;
  reclen = bytes2ulong(hdrptr);
  str_ready(&buf, reclen+HASH_SIZE);
  if (!ibuf_read(in, buf.s, reclen+HASH_SIZE))
    die1sys(1, "Could not read record data.");
  hash_init(&hash);
  hash_update(&hash, header, HEADER_SIZE);
  hash_update(&hash, buf.s, reclen);
  hash_finish(&hash, hcmp);
  if (memcmp(buf.s+reclen, hcmp, HASH_SIZE))
    die1(1, "Record data was corrupted (check code mismatch).");

  handle_record(typeflags, strnum, recnum, reclen, buf.s);
  global_recnum++;
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
  unsigned char header[HEADER_SIZE];
  if (!ibuf_read(in, header, HEADER_SIZE)) return 0;
  if (bytes2ulong(header) == 0) return 0;
  do {
    if (!read_record(header, in)) return 0;
    if (!ibuf_read(in, header, HEADER_SIZE)) return 0;
  } while (bytes2ulong(header) != 0);
  return skip_page(in);
}

void read_journal(const char* filename)
{
  stream* h;
  char header[8+4+4+4+4+HASH_SIZE];
  char hashbuf[HASH_SIZE];
  HASH_CTX hash;
  ibuf in;

  if (!ibuf_open(&in, filename, 0))
    die3sys(1, "Could not open '", filename, "'");

  /* Read/validate header record */
  hash_init(&hash);
  if (!ibuf_read(&in, header, sizeof header))
    die3sys(1, "Could not read header from '", filename, "'");
  hash_update(&hash, header, sizeof header - HASH_SIZE);
  hash_finish(&hash, hashbuf);
  if (memcmp(header + 24, hashbuf, HASH_SIZE) != 0)
    die3(1, "'", filename, "' has invalid header check code");
  if (memcmp(header, "journald", 8) != 0)
    die3(1, "'", filename, "' is not a journald file (missing signature)");
  if (bytes2ulong(header+8) != 2)
    die3(1, "'", filename, "' is not a version 2 journald file");
  if ((pagesize = bytes2ulong(header+12)) == 0)
    die3(1, "'", filename, "' has zero page size");
  global_recnum = bytes2ulong(header+16);
  if (bytes2ulong(header+20) != 0)
    die3(1, "'", filename, "' has non-zero options length, can't handle it");
  
  if (!skip_page(&in))
    die3sys(1, "Could not skip first page of '", filename, "'");

  while (read_transaction(&in))
    ;
  ibuf_close(&in);

  if (streams) {
    warn3("Premature end of data in journal '", filename, "'");
    for (h = streams; h != 0; h = h->next)
      abort_stream(h);
  }
}


int cli_main(int argc, char* argv[])
{
  reader_argc = argc - 1;
  reader_argv = argv + 1;
  read_journal(argv[0]);
  obuf_flush(&outbuf);
  return 0;
}
