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
#include "writer.h"

#define HEADER_SIZE (1+4+4+4)

// static unsigned char saved_type = 0;
// static unsigned long saved_pos;
static unsigned long pageoff;

#define RECORD_EOJ    0
#define RECORD_INFO  'I'
#define RECORD_DATA  'D'
#define RECORD_ONCE  'O'
#define RECORD_END   'E'
#define RECORD_ABORT 'A'

static unsigned char* ulong2bytes(unsigned long v, unsigned char bytes[4])
{
  bytes[3] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[2] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[1] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[0] = (unsigned char)(v & 0xff);
  return bytes + 4;
}

static int writer_write(const unsigned char* data, unsigned long bytes)
{
  unsigned long available;
  while (bytes) {
    available = writer_pagesize - pageoff;
    if (bytes >= available) {
      memcpy(writer_pagebuf + pageoff, data, available);
      pageoff = 0;
      if (!writer_writepage()) return 0;
      data += available;
      bytes -= available;
      available = writer_pagesize - pageoff;
    }
    else {
      memcpy(writer_pagebuf + pageoff, data, bytes);
      pageoff += bytes;
      break;
    }
  }
  return 1;
}

static int write_record_raw(char type,
			    unsigned long stream, unsigned long record,
			    unsigned long buflen, const char* buf)
{
  HASH_CTX hash;
  unsigned char header[HEADER_SIZE];
  unsigned char hashbuf[HASH_SIZE];

  hash_init(&hash);
  /* hash/write the header */
  header[0] = type;
  ulong2bytes(stream, header+1);
  ulong2bytes(record, header+5);
  ulong2bytes(buflen, header+9);
  hash_update(&hash, header, HEADER_SIZE);
#if 0
  // FIXME: twopass
  if (opt_twopass && saved_type == 0) {
    saved_pos = writer_pos;
    saved_type = type;
    header[0] = RECORD_EOJ;
  }
#endif
  if (!writer_write(header, HEADER_SIZE)) return 0;

  /* hash/write the data */
  hash_update(&hash, buf, buflen);
  if (!writer_write(buf, buflen)) return 0;

  /* finish the hash and write it */
  hash_finish(&hash, hashbuf);
  if (!writer_write(hashbuf, HASH_SIZE)) return 0;

  return 1;
}

static int write_ident(connection* con)
{
  static char buf[4+IDENTSIZE];
  ulong2bytes(con->total, buf);
  memcpy(buf+4, con->ident, con->ident_len);
  return write_record_raw(RECORD_INFO, con->number, 0, con->ident_len+4, buf);
}

static int sync_journal(void)
{
  unsigned long prev;
  if (pageoff) {
    memset(writer_pagebuf+pageoff, 0, writer_pagesize-pageoff);
    if (!writer_writepage()) return 0;
  }
  pageoff = 0;
  prev = writer_pos;
  memset(writer_pagebuf, 0, writer_pagesize);
  if (!writer_writepage()) return 0;
  if (!writer_sync()) return 0;
  if (!writer_seek(prev)) return 0;
  return 1;
}

int rotate_journal(void)
{
  unsigned i;

  if (!sync_records()) return 0;
  sync();
  if (!writer_seek(writer_pagesize)) return 0;
  if (!sync_journal()) return 0;
  
  for (i = 0; i < opt_connections; i++)
    connections[i].wrote_ident = 0;
  return 1;
}

static void make_file_header(void)
{
  unsigned char* p = writer_pagebuf;
  HASH_CTX hash;
  memset(p, 0, writer_pagesize);
  memcpy(p, "journald", 8); p += 8;
  p = ulong2bytes(2, p);
  p = ulong2bytes(writer_pagesize, p);
  // FIXME: fill in real option flags
  p = ulong2bytes(0, p);
  *p++ = 0;
  hash_init(&hash);
  hash_update(&hash, writer_pagebuf, p - writer_pagebuf);
  hash_finish(&hash, p); p += HASH_SIZE;
  pageoff = p - writer_pagebuf;
}

int open_journal(const char* filename)
{
  if (writer_init(filename) == 0) return 0;
  if (writer_size < writer_pagesize * 4) return 0;
  make_file_header();
  if (!sync_journal()) return 0;
  return 1;
}

static int check_rotate(unsigned long buflen)
{
  if (writer_pos + pageoff +
      HEADER_SIZE + buflen + HASH_SIZE + 1 + writer_pagesize >= writer_size)
    if (!rotate_journal())
      return 0;
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

  if (!check_rotate(con->buf_length)) return 0;

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
  // FIXME: unsigned long orig_pos;
  // FIXME: if (opt_twopass && saved_type == 0) return 1;

  /* Sync the data */
  if (!sync_journal()) return 0;

#if 0
  // FIXME: twopass
  if (opt_twopass) {
    /* Then restore the original starting record type byte and sync again */
    orig_pos = writer_pos;
    if (!writer_seek(saved_pos) ||
	!writer_write(&saved_type, 1) ||
	!writer_sync() ||
	!writer_seek(orig_pos))
      return 0;
    saved_type = 0;
  }
#endif
  return 1;
}
