#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "crc32.h"
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

static CRC32 set_iov(struct iovec* iov, char* data, unsigned long size,
		     CRC32 crc)
{
  iov->iov_base = data;
  iov->iov_len = size;
  return CRC32_block(crc, data, size);
}

int write_record(connection* con, int final, int abort)
{
  char type[1];
  struct iovec iov[7];
  unsigned char ilen[4];
  unsigned char rlen[4];
  unsigned char recnum[4];
  unsigned char crcbytes[4];
  CRC32 crc;
  unsigned long wr;
  unsigned long reclen;
  
  if (abort) {
    con->buf_length = 0;
    if (!con->not_first) return 1;
    type[0] = 'A';
  }
  else
    type[0] = final ?
      (con->not_first ? 'E' : 'O') : (con->not_first ? 'C' : 'S');

  ulong2bytes(con->ident_len, ilen);
  ulong2bytes(con->buf_length, rlen);
  ulong2bytes(record_number, recnum);
  crc = CRC32init;
  crc = set_iov(iov+0, type, 1, crc);
  crc = set_iov(iov+1, recnum, 4, crc);
  crc = set_iov(iov+2, ilen, 4, crc);
  crc = set_iov(iov+3, rlen, 4, crc);
  crc = set_iov(iov+4, con->ident, con->ident_len, crc);
  crc = set_iov(iov+5, con->buf, con->buf_length, crc);
  ulong2bytes(crc, crcbytes);
  set_iov(iov+6, crcbytes, 4, crc);
  
  if (opt_twopass && !saved_type) {
    saved_type = type[0];
    type[0] = 0;
  }

  reclen = 1+4+4+4+con->ident_len+con->buf_length+4;
  if (journal_size+reclen >= opt_maxsize)
    if (!open_journal()) return 0;
  
  if ((wr = writev(fdout, iov, 7)) != reclen) return 0;
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
