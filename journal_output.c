#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "crc32.h"
#include "journald.h"

static int fdout = -1;
static const char header[16] = "journald" "\0\0\0\1" "\0\0\0\0";

int open_journal(const char* path)
{
  if (chdir(path)) return 0;
  fdout = open("current", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fdout == -1) return 0;
  if (write(fdout, header, sizeof header) != sizeof header) {
    close(fdout);
    return 0;
  }
  return 1;
}

static void ulong2bytes(unsigned long v, char bytes[4])
{
  bytes[3] = v & 0xff; v >>= 8;
  bytes[2] = v & 0xff; v >>= 8;
  bytes[1] = v & 0xff; v >>= 8;
  bytes[0] = v;
}

static CRC32 set_iov(struct iovec* iov, char* data, long size, CRC32 crc)
{
  iov->iov_base = data;
  iov->iov_len = size;
  return CRC32_block(crc, data, size);
}

static char saved_type = 0;
static long bytes_written = 0;

int write_record(connection* con, int final, int abort)
{
  char type[1];
  struct iovec iov[6];
  unsigned char ilen[4];
  unsigned char rlen[4];
  unsigned char crcbytes[4];
  CRC32 crc;
  long wr;
  
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
  crc = CRC32init;
  crc = set_iov(iov+0, type, 1, crc);
  crc = set_iov(iov+1, ilen, 4, crc);
  crc = set_iov(iov+2, rlen, 4, crc);
  crc = set_iov(iov+3, con->ident, con->ident_len, crc);
  crc = set_iov(iov+4, con->buf, con->buf_length, crc);
  ulong2bytes(crc, crcbytes);
  set_iov(iov+5, crcbytes, 4, crc);
  
  if (!saved_type) {
    saved_type = type[0];
    type[0] = 0;
  }
  if ((wr = writev(fdout, iov, 6)) != 1+4+4+con->ident_len+con->buf_length+4)
    return 0;
  bytes_written += wr;
  con->total += con->buf_length;
  con->records++;
  con->not_first = 1;
  con->buf_length = 0;
  return 1;
}

int sync_records(void)
{
  char type[1];
  if (!saved_type) return 1;

  /* First, append an empty record marker and sync the data */
  type[0] = 0;
  if (write(fdout, type, 1) != 1) return 0;
  if (fdatasync(fdout)) return 0;

  /* Then restore the original starting record type byte and sync again */
  if (lseek(fdout, -(bytes_written+1), SEEK_CUR) == -1) return 0;
  type[0] = saved_type;
  if (write(fdout, type, 1) != 1) return 0;
  if (fdatasync(fdout)) return 0;
  saved_type = 0;

  /* Finally, seek back to where this routine started. */
  if (lseek(fdout, bytes_written-1, SEEK_CUR) == -1) return 0;
  bytes_written = 0;
  return 1;
}
