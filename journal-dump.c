/* journal-read.c - Dump the low-level contents of a journal.
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
#include <stdlib.h>
#include <string.h>

#include <iobuf/iobuf.h>

#include "reader.h"

const char program[] = "journal-dump";

static void obuf_putstream(obuf* out, const stream* s, const char* end)
{
  obuf_puts(out, "stream ");
  obuf_putuw(out, s->strnum, 10, ' ');
  obuf_puts(out, " record ");
  obuf_putuw(out, s->recnum, 10, ' ');
  obuf_puts(out, " offset ");
  obuf_putuw(out, s->offset, 10, ' ');
  obuf_putc(out, ' ');
  obuf_puts(out, end);
}

void end_stream(stream* s)
{
  obuf_putstream(&outbuf, s, "end\n");
}

void abort_stream(stream* s)
{
  obuf_putstream(&outbuf, s, "abort\n");
}

void init_stream(stream* s)
{
  obuf_putstream(&outbuf, s, "init ident(");
  obuf_putu(&outbuf, s->identlen);
  obuf_puts(&outbuf, ")='");
  obuf_write(&outbuf, s->ident, s->identlen);
  obuf_puts(&outbuf, "'\n");
}

void append_stream(stream* s, const char* buf, unsigned long reclen)
{
  obuf_putstream(&outbuf, s, "append bytes ");
  obuf_putu(&outbuf, reclen);
  obuf_putc(&outbuf, LF);
}

static void usage(const char* msg)
{
  if (msg)
    obuf_put4s(&errbuf, program, ": ", msg, "\n");
  obuf_put3s(&errbuf, "usage: ", program, " filename\n");
  obuf_flush(&errbuf);
  exit(1);
}

int main(int argc, char* argv[])
{
  if (argc != 2) usage(0);
  read_journal(argv[1]);
  obuf_flush(&outbuf);
  return 0;
}
