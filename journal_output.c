#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "crc32.h"
#include "journald.h"

static int fdout = -1;

static char* itoa(long i)
{
  static char buf[30];
  char* ptr = buf + sizeof buf - 1;
  *ptr-- = 0;
  while(i > 0) {
    *ptr-- = (i % 10) + '0';
    i /= 10;
  }
  return ptr + 1;
}

static char saved_type = 0;
static long bytes_written = 0;

// TODO:
// - Open the output file
// - Temporarily mark the first part of a transaction
// - Rotate the output file after writing
// - Partial writes

int write_record(connection* con, int final, int abort)
{
  char type[1];
  struct iovec iov[5];
  char* lengthstr;
  CRC32 crc;
  long wr;
  
  if (abort) {
    type[0] = 'A';
    con->buf_length = 0;
  }
  else if (final) {
    if (!con->buf_offset)
      type[0] = 'O';
    else
      type[0] = 'E';
  }
  else {
    if (!con->buf_offset)
      type[0] = 'S';
    else
      type[0] = 'C';
  }
  
  lengthstr = itoa(con->buf_length);
  crc = CRC32init;
  crc = CRC32_block(crc, type, 1);
  iov[0].iov_base = type;
  iov[0].iov_len = 1;

  crc = CRC32_block(crc, con->ident, con->ident_len+1);
  iov[1].iov_base = con->ident;
  iov[1].iov_len = con->ident_len+1;

  crc = CRC32_block(crc, lengthstr, strlen(lengthstr)+1);
  iov[2].iov_base = lengthstr;
  iov[2].iov_len = strlen(lengthstr)+1;

  crc = CRC32_block(crc, con->buf, con->buf_length);
  iov[3].iov_base = con->buf;
  iov[3].iov_len = con->buf_length;

  iov[4].iov_base = &crc;
  iov[4].iov_len = 4;
  
  if (!saved_type) {
    saved_type = type[0];
    type[0] = 0;
  }
  wr = writev(fdout, iov, 5);
  if (!wr || wr == -1) return -1;
  bytes_written += wr;
  con->buf_offset += con->buf_length;
  return 0;
}

int sync_records(void)
{
  char type[1];
  if (!saved_type) return 0;

  /* First, append an empty record marker and sync the data */
  type[0] = 0;
  if (write(fdout, type, 1) != 1) return -1;
  if (fdatasync(fdout)) return -1;

  /* Then restore the original starting record type byte and sync again */
  if (lseek(fdout, -(bytes_written+1), SEEK_CUR) == -1) return -1;
  type[0] = saved_type;
  if (write(fdout, type, 1) != 1) return -1;
  if (fdatasync(fdout)) return -1;
  saved_type = 0;
  bytes_written = 0;

  /* Finally, seek back to where this routine started. */
  if (lseek(fdout, bytes_written, SEEK_CUR) == -1) return -1;
  return 0;
}
