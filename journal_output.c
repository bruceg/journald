/* journal_output.c - Journal file output and synchronization.
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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "hash.h"
#include "journald_server.h"

static int fdout = 0;
static char header[16] = "journald" "\0\0\0\1" "\0\0\0\0";
static unsigned long file_nr = 0;

static char saved_type = 0;
static unsigned long transaction_size = 0;
static unsigned long journal_size = 0;

static void ulong2bytes(unsigned long v, unsigned char bytes[4])
{
  bytes[3] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[2] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[1] = (unsigned char)(v & 0xff); v >>= 8;
  bytes[0] = (unsigned char)(v & 0xff);
}

static int fsynccwd(void)
{
  int fd;
  int res;
  if ((fd = open(".", O_RDONLY)) == -1) return 0;
  res = !(fsync(fd) == -1 && errno == EIO);
  close(fd);
  return res;
}

int rotate_journal(void)
{
  static char buf[60];
  char* ptr;
  struct timeval tv;
  unsigned i;

  if (fdout) {
    if (!sync_records()) return 0;
    if (close(fdout)) return 0;
    fdout = 0;
  }
  
  ptr = buf + 29;
  *ptr-- = 0;
  gettimeofday(&tv, 0);
  for (i = 0; i < 6; i++, tv.tv_usec /= 10)
    *ptr-- = (tv.tv_usec % 10) + '0';
  for (*ptr-- = '.'; tv.tv_sec; tv.tv_sec /= 10)
    *ptr-- = (tv.tv_sec % 10) + '0';
  ++ptr;

  if (link("current", ptr)) return 0;
  if (unlink("current")) return 0;
  ++file_nr;
  if (!fsynccwd()) return 0;
  for (i = 0; i < opt_connections; i++)
    connections[i].wrote_ident = 0;
  return 1;
}

int open_journal(void)
{
  static struct stat statbuf;
  if (fdout || !stat("current", &statbuf))
    if (!rotate_journal()) return 0;
  fdout = open("current", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fdout == -1) return 0;
  if (!fsynccwd()) return 0;
  ulong2bytes(file_nr, header+12);
  if (write(fdout, header, sizeof header) != sizeof header) {
    close(fdout);
    return 0;
  }
  journal_size = 0;
  return 1;
}

static void set_iov(struct iovec* iov, char* data, unsigned long size,
		    HASH_CTX* hash)
{
  iov->iov_base = data;
  iov->iov_len = size;
  hash_update(hash, data, size);
}

static int write_record_raw(char type,
			    unsigned long stream, unsigned long record,
			    unsigned long buflen, const char* buf)
{
  static struct iovec iov[3];
  static unsigned char hdr[1+4+4+4];
  static unsigned char hashbytes[HASH_SIZE];
  static HASH_CTX hash;
  unsigned long wr;
  unsigned long reclen;
  unsigned char* hdrptr;
  
  hdrptr = hdr;
  *hdrptr++ = type;
  ulong2bytes(stream, hdrptr); hdrptr += 4;
  ulong2bytes(record, hdrptr); hdrptr += 4;
  ulong2bytes(buflen, hdrptr);
  hash_init(&hash);
  set_iov(iov+0, hdr, sizeof hdr, &hash);
  set_iov(iov+1, (char*)buf, buflen, &hash);
  hash_finish(&hash, hashbytes);
  iov[2].iov_base = hashbytes;
  iov[2].iov_len = HASH_SIZE;
  
  if (opt_twopass && !saved_type) {
    saved_type = type;
    type = 0;
  }

  reclen = sizeof hdr + buflen + HASH_SIZE;
  if ((wr = writev(fdout, iov, 3)) != reclen) return 0;
  journal_size += reclen;
  transaction_size += reclen;
  return 1;
}

static int write_ident(connection* con)
{
  static char buf[4+IDENTSIZE];
  ulong2bytes(con->total, buf);
  memcpy(buf+4, con->ident, con->ident_len);
  return write_record_raw('I', con->number, 0, con->ident_len+4, buf);
}

int write_record(connection* con, int final, int abort)
{
  char type;
  
  if (abort) {
    con->buf_length = 0;
    if (!con->not_first) return 1;
    type = 'A';
  }
  else
    type = final ?
      (con->not_first ? 'E' : 'O') : (con->not_first ? 'C' : 'S');

  if (journal_size + con->buf_length >= opt_maxsize)
    if (!open_journal()) return 0;
  
  if (!con->wrote_ident) {
    if (!write_ident(con)) return 0;
    con->wrote_ident = 1;
  }
  
  if (!write_record_raw(type, con->number, con->records,
			con->buf_length, con->buf)) return 0;
  
  con->total += con->buf_length;
  con->records++;
  con->not_first = 1;
  con->buf_length = 0;
  return 1;
}

int sync_records(void)
{
  char type[1];
  if (opt_twopass && !saved_type) return 1;

  /* First, append an empty record marker and sync the data */
  type[0] = 0;
  if (write(fdout, type, 1) != 1) return 0;
  if (fdatasync(fdout)) return 0;

  if (opt_twopass) {
    /* Then restore the original starting record type byte and sync again */
    if (lseek(fdout, -(transaction_size+1), SEEK_CUR) == -1) return 0;
    type[0] = saved_type;
    if (write(fdout, type, 1) != 1) return 0;
    if (fdatasync(fdout)) return 0;
    saved_type = 0;

    /* Finally, seek back to where this routine started. */
    if (lseek(fdout, transaction_size-1, SEEK_CUR) == -1) return 0;
  }
  else
    if (lseek(fdout, -1, SEEK_CUR) == -1) return 0;
  
  transaction_size = 0;
  return 1;
}
