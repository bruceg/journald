/* journal-read.c - Process a journal
   Copyright (C) 2000,2002 Bruce Guenter

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
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>

#include <cli/cli.h>
#include <msg/msg.h>

#include "reader.h"

static char** argv = 0;

const char program[] = "journal-read";
const char cli_help_prefix[] = "Sends journal streams through a program\n";
const char cli_help_suffix[] = "";
const char cli_args_usage[] = "filename program [args ...]";
const int cli_args_min = 2;
const int cli_args_max = -1;
int msg_debug_bits;
cli_option cli_options[] = {
  { 'd', "debug", CLI_FLAG, DEBUG_JOURNAL, &msg_debug_bits,
    "Turn on some debugging messages", 0 },
  {0,0,0,0,0,0,0}
};

static char* ulongtoa(unsigned long i)
{
  static char buf[21];
  char* ptr;
  if (!i) return "0";
  ptr = buf+20;
  *ptr-- = 0;
  while (i) {
    *ptr-- = (i % 10) + '0';
    i /= 10;
  }
  return ptr+1;
}

void copy_argv(void)
{
  int i;
  if ((argv = malloc(sizeof *argv * (reader_argc+3))) == 0)
    die1(1, "Out of memory");
  for (i = 0; i < reader_argc; i++)
    argv[i] = reader_argv[i];
}

void end_stream(stream* s)
{
  pid_t pid;
  int fd;
  
  fd = *(int*)(s->data);

  if (!argv) copy_argv();
  if (lseek(fd, 0, SEEK_SET) != 0) die1sys(1, "lseek failed");
  if ((pid = fork()) == -1) die1sys(1, "fork failed");
  if (!pid) {
    close(0);
    dup2(fd, 0);
    close(fd);
    argv[reader_argc+0] = s->ident;
    argv[reader_argc+1] = ulongtoa(s->start_offset);
    argv[reader_argc+2] = 0;
    execvp(argv[0], argv);
    die1sys(1, "exec failed");
  }
  if (waitpid(pid, 0, WUNTRACED) != pid) die1sys(1, "waitpid failed");
  close(fd);
  free(s->data);
}

void abort_stream(stream* s)
{
  int fd;
  fd = *(int*)(s->data);
  close(fd);
}

void init_stream(stream* s)
{
  char filename[] = "journal-read.tmp.XXXXXX";
  int fd;
  if ((fd = mkstemp(filename)) == -1) die1sys(1, "mkstemp failed");
  if (unlink(filename)) die1sys(1, "unlink failed");
  s->data = malloc(sizeof(int));
  *(int*)(s->data) = fd;
}

void append_stream(stream* s, const char* buf, uint32 reclen)
{
  int fd;
  fd = *(int*)(s->data);
  if ((uint32)write(fd, buf, reclen) != reclen)
    die1sys(1, "write to temporary file failed");
}
