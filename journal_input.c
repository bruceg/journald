#include "journald.h"

int needs_fsync = 0;

static int read_identifier(connection* con, char byte)
{
  con->ident[con->ident_len++] = byte;
  return byte ? 0 : 1;
}

static int read_init_length(connection* con, char byte)
{
  if (byte) {
    if (byte >= '0' && byte <= '9') {
      con->length = con->length*10 + byte - '0';
      return 1;
    }
    else
      return -1;
  }
  else
    return con->length ? 3 : 2;
}

static int read_only_record(connection* con, char byte)
{
  con->buf[con->buf_length++] = byte;
  if (con->buf_offset + con->buf_length == con->length) {
    con->ok = !write_record(con, 1, 0);
    if (con->ok) needs_fsync = 1;
    return -1;
  }
  if (con->buf_length == CBUFSIZE) {
    if (write_record(con, 0, 0))
      return -1;
  }
  return 2;
}

static int read_next_length(connection* con, char byte)
{
  if (byte) {
    if (byte >= '0' && byte <= '9') {
      con->length = con->length*10 + byte - '0';
      return 3;
    }
    else {
      write_record(con, 0, 1);
      return -1;
    }
  }
  else {
    if (!con->length) {
      con->ok = !write_record(con, 1, 0);
      if (con->ok) needs_fsync = 1;
      return -1;
    }
    else
      return 4;
  }
}

static int read_next_record(connection* con, char byte)
{
  con->buf[con->buf_length++] = byte;
  if (con->buf_offset + con->buf_length == con->length) {
    if (write_record(con, 0, 0))
      return -1;
    else
      return 3;
  }
  if (con->buf_length == CBUFSIZE) {
    if (write_record(con, 0, 0))
      return -1;
  }
  return 4;
}

void handle_data(connection* con, char* data, long size)
{
  int state;
  if (!size) {
    write_record(con, 0, 1);
  }
  else {
    for (state = con->state; size > 0; ++data, --size)
      switch (state) {
      case 0: state = read_identifier(con, *data); continue;
      case 1: state = read_init_length(con, *data); continue;
      case 2: state = read_only_record(con, *data); continue;
      case 3: state = read_next_length(con, *data); continue;
      case 4: state = read_next_record(con, *data); continue;
      }
  }
}
