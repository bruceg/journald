#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "hash.h"
#include "journald.h"

static int fdout = 0;
static char header[16] = "journald" "\0\0\0\1" "\0\0\0\0";
static unsigned long file_nr = 0;

static char saved_type = 0;
static unsigned long transaction_size = 0;
static unsigned long journal_size = 0;
static unsigned long record_number = 0;

static void ulong2bytes(unsigned long v, char bytes[4])
{
  bytes[3] = v & 0xff; v >>= 8;
  bytes[2] = v & 0xff; v >>= 8;
  bytes[1] = v & 0xff; v >>= 8;
  bytes[0] = v;
}

static int rotate_journal(void)
{
  static char buf[60];
  char* ptr;
  struct timeval tv;
  int i;

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
  return 1;
}

int open_journal(void)
{
  static struct stat statbuf;
  if (fdout || !stat("current", &statbuf))
    if (!rotate_journal()) return 0;
  fdout = open("current", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fdout == -1) return 0;
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

int write_record(connection* con, int final, int abort)
{
  char type;
  struct iovec iov[4];
  unsigned char hdr[1+4+4+4];
  unsigned char hashbytes[HASH_SIZE];
  HASH_CTX hash;
  unsigned long wr;
  unsigned long reclen;
  
  if (abort) {
    con->buf_length = 0;
    if (!con->not_first) return 1;
    type = 'A';
  }
  else
    type = final ?
      (con->not_first ? 'E' : 'O') : (con->not_first ? 'C' : 'S');

  hdr[0] = type;
  ulong2bytes(record_number, hdr+1);
  ulong2bytes(con->ident_len, hdr+5);
  ulong2bytes(con->buf_length, hdr+9);
  hash_init(&hash);
  set_iov(iov+0, hdr, sizeof hdr, &hash);
  set_iov(iov+1, con->ident, con->ident_len, &hash);
  set_iov(iov+2, con->buf, con->buf_length, &hash);
  hash_finish(&hash, hashbytes);
  iov[3].iov_base = hashbytes;
  iov[3].iov_len = HASH_SIZE;
  
  if (opt_twopass && !saved_type) {
    saved_type = type;
    type = 0;
  }

  reclen = sizeof hdr + con->ident_len + con->buf_length + HASH_SIZE;
  if (journal_size+reclen >= opt_maxsize)
    if (!open_journal()) return 0;
  
  if ((wr = writev(fdout, iov, 4)) != reclen) return 0;
  journal_size += reclen;
  transaction_size += reclen;
  record_number++;
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
