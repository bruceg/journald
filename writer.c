/* writer.c - Journal file output and synchronization.
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

#include <uint32.h>
#include "flags.h"
#include "hash.h"
#include "server.h"
#include "writer.h"

static uint32 pageoff;

static uint32 global_recnum = 0;

static int writer_write(const unsigned char* data, uint32 bytes)
{
  uint32 available;
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

static int write_record_raw(uint32 type,
			    uint32 stream, uint32 record,
			    uint32 buflen, const char* buf)
{
  HASH_CTX hash;
  unsigned char header[HEADER_SIZE];
  unsigned char hashbuf[HASH_SIZE];

  hash_init(&hash);
  /* hash/write the header */
  uint32_pack_lsb(type, header);
  uint32_pack_lsb(global_recnum, header+4);
  uint32_pack_lsb(stream, header+8);
  uint32_pack_lsb(record, header+12);
  uint32_pack_lsb(buflen, header+16);
  hash_update(&hash, header, HEADER_SIZE);
  if (!writer_write(header, HEADER_SIZE)) return 0;

  /* hash/write the data */
  hash_update(&hash, buf, buflen);
  if (!writer_write(buf, buflen)) return 0;

  /* finish the hash and write it */
  hash_finish(&hash, hashbuf);
  if (!writer_write(hashbuf, HASH_SIZE)) return 0;

  global_recnum++;
  return 1;
}

static int write_ident(connection* con)
{
  static char buf[4+IDENTSIZE];
  uint32_pack_lsb(con->total, buf);
  memcpy(buf+4, con->ident, con->ident_len);
  return write_record_raw(RECORD_INFO, con->number, con->records,
			  con->ident_len+4, buf);
}

int sync_records(void)
{
  uint32 prev;
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

static void make_file_header(void)
{
  unsigned char* p = writer_pagebuf;
  HASH_CTX hash;
  memset(p, 0, writer_pagesize);
  memcpy(p, "journald", 8); p += 8;
  uint32_pack_lsb(2, p); p += 4;
  uint32_pack_lsb(writer_pagesize, p); p += 4;
  uint32_pack_lsb(global_recnum, p); p += 4;
  uint32_pack_lsb(0, p); p += 4;
  hash_init(&hash);
  hash_update(&hash, writer_pagebuf, p - writer_pagebuf);
  hash_finish(&hash, p); p += HASH_SIZE;
  pageoff = p - writer_pagebuf;
}

int rotate_journal(void)
{
  unsigned i;

  if (!sync_records()) return 0;
  sync();
  if (!writer_seek(0)) return 0;
  make_file_header();
  if (!sync_records()) return 0;
  
  for (i = 0; i < opt_connections; i++)
    connections[i].wrote_ident = 0;
  return 1;
}

int open_journal(const char* filename)
{
  if (writer_init(filename) == 0) return 0;
  if (writer_size < writer_pagesize * 4) return 0;
  make_file_header();
  if (!sync_records()) return 0;
  return 1;
}

static int check_rotate(uint32 buflen)
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
  else {
    type = 0;
    if (con->buf_length)
      type |= RECORD_DATA;
    if (final)
      type |= RECORD_EOS;
  }

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

  if (!check_rotate(1)) return 0;
  return 1;
}
