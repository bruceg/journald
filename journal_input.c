#include <stdio.h>
#include "journald.h"

/*
  TODO:
  - Rewrite the state functions to read multiple bytes at once
*/

/*
  Per-connection algorithm:
  - Accept connection
  - Read in identifier length (state 0: reading identifier length)
  - Read in identifier string (state 1: reading identifier)
  - read in record length (state 2: reading next record length)
  - while length is not zero:
    - read in record (state 3: reading record)
    - write out journal record
    - read in next record length (state 2: reading next record length)
  - Send OK code (state -1: sending response)
  - Close connection
*/

int needs_fsync = 0;

static int read_ident_length(connection* con, char byte)
{
  con->ident_len <<= 8;
  con->ident_len |= (unsigned char)byte;
  con->count++;
  if (con->count == 4) {
    con->count = 0;
    return 1;
  }
  else
    return 0;
}

static int read_ident(connection* con, char byte)
{
  con->ident[con->count++] = byte;
  if (con->count == con->ident_len) {
    con->count = 0;
    con->length = 0;
    return 2;
  }
  else
    return 1;
}

static int read_record_length(connection* con, char byte)
{
  con->length <<= 8;
  con->length |= (unsigned char)byte;
  con->count++;
  if (con->count == 4) {
    if (con->length) {
      con->count = 0;
      return 3;
    }
    else {
      con->ok = write_record(con, 1, 0);
      if (con->ok)
	needs_fsync = 1;
      return -1;
    }
  }
  else
    return 2;
}

static int read_record(connection* con, char byte)
{
  con->buf[con->buf_length++] = byte;
  con->count++;
  if (con->buf_length == CBUFSIZE) {
    if (!write_record(con, 0, 0))
      return -1;
  }
  if (con->count == con->length) {
    con->count = 0;
    con->length = 0;
    return 2;
  }
  return 3;
}

#define DEBUG

void handle_data(connection* con, char* data, long size)
{
  int state;
  if (!size) {
    write_record(con, 0, 1);
  }
  else {
    for (state = con->state; size > 0; ++data, --size) {
#ifdef DEBUG
      printf("state=%d byte=%d(%c) length=%ld buf_length=%ld count=%d ",
	     state, *data, *data, con->length, con->buf_length, con->count);
#endif
      switch (state) {
      case 0: state = read_ident_length(con, *data); break;
      case 1: state = read_ident(con, *data); break;
      case 2: state = read_record_length(con, *data); break;
      case 3: state = read_record(con, *data); break;
      default: die("Invalid state in handle_data");
      }
#ifdef DEBUG
      printf("next=%d\n", state);
#endif
    }
    con->state = state;
#ifdef DEBUG
    fflush(stdout);
#endif
  }
}
