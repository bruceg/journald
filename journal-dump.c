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

#include <cli/cli.h>
#include <iobuf/iobuf.h>

#include "reader.h"

const char program[] = "journal-dump";
const char cli_help_prefix[] = "Dumps the low-level contents of a journal\n";
const char cli_help_suffix[] = "";
const char cli_args_usage[] = "filename";
const int cli_args_min = 1;
const int cli_args_max = 1;
int msg_debug_bits;
cli_option cli_options[] = {
  { 'd', "debug", CLI_FLAG, DEBUG_JOURNAL, &msg_debug_bits,
    "Turn on some debugging messages", 0 },
  {0,0,0,0,0,0,0}
};

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
