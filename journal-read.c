/* journal-read.c - Dump the contents of journal directories.
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>

#include <msg/msg.h>

#include "reader.h"

static int opt_argc;
static char** opt_argv;

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

const char program[] = "journal-read";

void end_stream(stream* s)
{
  pid_t pid;
  int fd;
  
  fd = *(int*)(s->data);
  
  if (lseek(fd, 0, SEEK_SET) != 0) die1sys(1, "lseek");
  if ((pid = fork()) == -1) die1sys(1, "fork");
  if (!pid) {
    close(0);
    dup2(fd, 0);
    close(fd);
    opt_argv[opt_argc+0] = s->ident;
    opt_argv[opt_argc+1] = ulongtoa(s->start_offset);
    opt_argv[opt_argc+2] = 0;
    execvp(opt_argv[0], opt_argv);
    die1sys(1, "exec");
  }
  if (waitpid(pid, 0, WUNTRACED) != pid) die1sys(1, "waitpid");
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
  if ((fd = mkstemp(filename)) == -1) die1sys(1, "mkstemp");
  if (unlink(filename)) die1sys(1, "unlink");
  s->data = malloc(sizeof(int));
  *(int*)(s->data) = fd;
}

void append_stream(stream* s, const char* buf, unsigned long reclen)
{
  int fd;
  fd = *(int*)(s->data);
  if (write(fd, buf, reclen) != reclen)
    die1sys(1, "write to temporary file");
}

static void usage(const char* msg)
{
  if (msg)
    fprintf(stderr, "%s: %s\n", program, msg);
  fprintf(stderr, "usage: %s filename program [args ...]\n", program);
  exit(1);
}

int main(int argc, char* argv[])
{
  int i;
  if (argc < 3) usage("Too few command-line arguments");
  opt_argc = argc - 2;
  opt_argv = malloc(sizeof(char*) * (opt_argc+3));
  for (i = 0; i < opt_argc; i++)
    opt_argv[i] = argv[i+2];
  read_journal(argv[1]);
  return 0;
}
