/* journal_output.c - Journal file output and synchronization.
   Copyright (C) 2002 Bruce Guenter

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
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>
#include "hash.h"
#include "journald_server.h"

static int fdout = 0;
static char file_header[16] = "journald" "\0\0\0\2" "\0\0\0\0";

#define HEADER_SIZE (1+4+4+4)

static char saved_type = 0;
static unsigned long journal_syncpos;
static unsigned long journal_pos;
static unsigned long journal_size;
static unsigned char* journal;
static unsigned pagesize;
static unsigned pageshift;

#define RECORD_EOJ    0
#define RECORD_INFO  'I'
#define RECORD_DATA  'D'
#define RECORD_ONCE  'O'
#define RECORD_END   'E'
#define RECORD_ABORT 'A'

static void set_pageshift(void)
{
  unsigned i;
  for (pageshift = 0, i = pagesize = getpagesize();
       i != 0;
       ++pageshift, i >>= 1) ;
}
  
static void ulong2bytes(unsigned long v, unsigned char bytes[4])
{
  bytes[3] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[2] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[1] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[0] = (unsigned char)(v & 0xff);
}

static int write_record_raw(char type,
			    unsigned long stream, unsigned long record,
			    unsigned long buflen, const char* buf)
{
  static HASH_CTX hash;
  unsigned char* ptr;
  unsigned long reclen;
  
  reclen = HEADER_SIZE + buflen + HASH_SIZE;
  if (journal_pos + reclen + 1 >= journal_size)
    if (!rotate_journal()) return 0;

  ptr = journal + journal_pos;
  if (opt_twopass && saved_type == 0) {
    saved_type = type;
    ptr[0] = RECORD_EOJ;
  }
  else
    ptr[0] = type;
  ulong2bytes(stream, ptr+1);
  ulong2bytes(record, ptr+5);
  ulong2bytes(buflen, ptr+9);
  memcpy(ptr+HEADER_SIZE, buf, buflen);
  hash_init(&hash);
  hash_update(&hash, ptr, HEADER_SIZE+buflen);
  hash_finish(&hash, ptr+HEADER_SIZE+buflen);
  ptr[reclen] = RECORD_EOJ;

  journal_pos += reclen;
  return 1;
}

static int write_ident(connection* con)
{
  static char buf[4+IDENTSIZE];
  ulong2bytes(con->total, buf);
  memcpy(buf+4, con->ident, con->ident_len);
  return write_record_raw(RECORD_INFO, con->number, 0, con->ident_len+4, buf);
}

static int mdatasync(unsigned long start, unsigned long end)
{
  start = (start >> pageshift) << pageshift;
  return msync(journal + start, end - start, MS_SYNC|MS_INVALIDATE) == 0;
}

static int sync_journal(void)
{
  journal[journal_pos] = RECORD_EOJ;
  return mdatasync(journal_syncpos, journal_pos + 1);
}

int rotate_journal(void)
{
  unsigned i;

  if (!sync_records()) return 0;
  sync();
  journal_syncpos = journal_pos = sizeof file_header;
  if (!sync_journal()) return 0;
  
  for (i = 0; i < opt_connections; i++)
    connections[i].wrote_ident = 0;
  return 1;
}

int open_journal(const char* filename)
{
  static struct stat statbuf;
  set_pageshift();
  if ((fdout = open(filename, O_RDWR)) == -1) return 0;
  if (fstat(fdout, &statbuf) == -1) return 0;
  journal_size = (statbuf.st_size >> pageshift) << pageshift;
  if ((journal = mmap(0, journal_size, PROT_WRITE, MAP_SHARED,
		      fdout, 0)) == (unsigned char*)-1)
    return 0;
  memcpy(journal, file_header, sizeof file_header);
  journal_syncpos = 0;
  journal_pos = sizeof file_header;
  if (!sync_journal()) return 0;
  journal_syncpos = journal_pos;
  return 1;
}

int write_record(connection* con, int final, int abort)
{
  char type;
  
  if (abort) {
    con->buf_length = 0;
    if (!con->total) return 1;
    type = RECORD_ABORT;
  }
  else
    type = final ? (con->total ? RECORD_END : RECORD_ONCE) : RECORD_DATA;

  if (!con->wrote_ident) {
    if (!write_ident(con)) return 0;
    con->wrote_ident = 1;
  }

  if (!write_record_raw(type, con->number, con->records,
			con->buf_length, con->buf)) return 0;
  
  con->total += con->buf_length;
  con->records++;
  con->buf_length = 0;
  return 1;
}

int sync_records(void)
{
  if (journal_syncpos == journal_pos) return 1;
  if (opt_twopass && saved_type == 0) return 1;

  /* Sync the data */
  if (!sync_journal()) return 0;

  if (opt_twopass) {
    /* Then restore the original starting record type byte and sync again */
    journal[journal_syncpos] = saved_type;
    if (!mdatasync(journal_syncpos, journal_syncpos + 1)) return 0;
    saved_type = 0;
  }
  journal_syncpos = journal_pos;
  return 1;
}
