/* journal_input.c - State machine for handling client input data.
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
#include <stdio.h>
#include "journald_server.h"

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

static unsigned long read_ident_length(connection* con,
				       unsigned char* bytes,
				       unsigned long size)
{
  unsigned long used;
  used = 0;
  while (size) {
    con->ident_len <<= 8;
    con->ident_len |= *bytes;
    con->count++;
    used++;
    bytes++;
    size--;
    if (con->count == 4) {
      con->count = 0;
      con->state = 1;
      break;
    }
  }
  return used;
}

static unsigned long read_ident(connection* con,
				unsigned char* bytes,
				unsigned long size)
{
  unsigned long used;

  used = con->ident_len - con->count;
  if (used > size) used = size;
  memcpy(con->ident+con->count, bytes, used);
  con->count += used;
  if (con->count == con->ident_len) {
    con->count = 0;
    con->length = 0;
    con->state = 2;
  }
  return used;
}

static unsigned long read_record_length(connection* con,
					unsigned char* bytes,
					unsigned long size)
{
  unsigned long used;
  used = 0;
  while (size) {
    con->length <<= 8;
    con->length |= *bytes;
    con->count++;
    used++;
    bytes++;
    size--;
    if (con->count == 4) {
      if (con->length) {
	con->count = 0;
	con->state = 3;
      }
      else {
	con->ok = write_record(con, 1, 0);
	con->state = -1;
      }
      break;
    }
  }
  return used;
}

static unsigned long read_record(connection* con,
				 unsigned char* bytes,
				 unsigned long size)
{
  unsigned long used;
  unsigned long use;
  used = 0;
  while (size) {
    if (con->buf_length == CBUFSIZE) {
      if (!write_record(con, 0, 0)) {
	con->state = -1;
	break;
      }
    }
    use = con->length - con->count;
    if (use > CBUFSIZE - con->buf_length) use = CBUFSIZE - con->buf_length;
    if (use > size) use = size;
    memcpy(con->buf + con->buf_length, bytes, use);
    bytes += use;
    size -= use;
    used += use;
    con->buf_length += use;
    con->count += use;
    if (con->count == con->length) {
      con->count = 0;
      con->length = 0;
      con->state = 2;
      break;
    }
  }
  return used;
}

void handle_data(connection* con, char* data, unsigned long size)
{
  unsigned long used;
  if (!size) {
    write_record(con, 0, 1);
  }
  else {
    while (size && con->state != -1) {
      used = 0;
#ifdef DEBUG
      printf("state=%d byte=%d length=%ld buf_length=%ld count=%d ",
	     con->state, *data, con->length, con->buf_length, con->count);
#endif
      switch (con->state) {
      case 0: used = read_ident_length(con, data, size); break;
      case 1: used = read_ident(con, data, size); break;
      case 2: used = read_record_length(con, data, size); break;
      case 3: used = read_record(con, data, size); break;
      default: die("Invalid state in handle_data");
      }
      size -= used;
      data += used;
#ifdef DEBUG
      printf("used=%ld next=%d\n", used, con->state);
#endif
    }
#ifdef DEBUG
    fflush(stdout);
#endif
  }
}
