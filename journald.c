/* journald.c - Journalling daemon socket handling and main loop.
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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cli/cli.h>
#include <msg/msg.h>
#include <str/str.h>

#include "server.h"
#include "writer.h"

extern void setup_env(int, const char*);

#ifndef SUN_LEN
/* Evaluate to actual length of the `sockaddr_un' structure.  */
#define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

static unsigned connection_count = 0;
static unsigned long connection_number = 0;

static unsigned long opt_timeout = 10*1000;
static unsigned opt_verbose = 0;
static unsigned opt_delete = 1;
static const char* opt_socket;
static uid_t opt_uid = -1;
static gid_t opt_gid = -1;
static int opt_envuidgid = 0;
static mode_t opt_umask = 0;
const char* opt_umask_str = 0;
static mode_t opt_mode = 0777;
const char* opt_mode_str = 0;
static int opt_backlog = 128;
static int opt_synconexit = 0;
static const char* opt_writer = "fdatasync";
unsigned opt_connections = 10;
connection* connections;

const char program[] = "journald";
const int msg_show_pid = 0;
const char cli_help_prefix[] = "Sends journal streams through a program\n";
const char cli_help_suffix[] =
"\nThe following writer methods are available:\n"
"  fdatasync:   Uses write and fdatasync to synchronize the data.\n"
"  mmap:        Uses mmap to access the data, and msync to synchronize.\n"
"  open+direct: Opens the journal in direct I/O mode (O_DIRECT).\n"
"  open+sync:   Opens the journal in synchronous write mode (O_DSYNC).\n";
const char cli_args_usage[] = "socket journal-file";
const int cli_args_min = 2;
const int cli_args_max = 2;
cli_option cli_options[] = {
  { 'u', "uid", CLI_UINTEGER, 0, &opt_uid,
    "Change user id to UID after creating socket", 0 },
  { 'g', "gid", CLI_UINTEGER, 0, &opt_gid,
    "Change group id to GID after creating socket", 0 },
  { 'U', "envuidgid", CLI_FLAG, 1, &opt_envuidgid,
    "Same as '-u $UID -g $GID'", 0 },
  { 'm', "mode", CLI_STRING, 0, &opt_mode_str,
    "Set mode of created socket (in octal)", 0 },
  { 0, "umask", CLI_STRING, 0, &opt_umask_str,
    "Set umask to MASK (in octal) before creating socket", 0 },
  { 'c', "concurrency", CLI_UINTEGER, 0, &opt_connections,
    "Do not handle more than N simultaneous connections", "10" },
  { 't', "pause", CLI_UINTEGER, 0, &opt_timeout,
    "Pause synchronization by N us", "10ms" },
  { 's', "synconexit", CLI_FLAG, 1, &opt_synconexit,
    "Sync on exit/interrupt", 0 },
  { 'w', "writer", CLI_STRING, 0, &opt_writer,
    "Writer synchronization method", "fdatasync" },
  { 'q', "quiet", CLI_FLAG, 0, &opt_verbose,
    "Turn off all but error messages", 0 },
  { 'v', "verbose", CLI_FLAG, 1, &opt_verbose,
    "Turn on all messages", 0 },
  { 0, "delete", CLI_FLAG, 1, &opt_delete,
    "Delete the socket on exit", 0 },
  { 0, "no-delete", CLI_FLAG, 0, &opt_delete,
    "Do not delete the socket on exit", 0 },
  {0,0,0,0,0,0,0}
};

static str msg;

static void log_status(void)
{
  if (opt_verbose) {
    str_copys(&msg, "status: ");
    str_catu(&msg, connection_count);
    str_catc(&msg, '/');
    str_catu(&msg, opt_connections);
    msg1(msg.s);
  }
}

static void close_connection(connection* con)
{
  close(con->fd);
  --connection_count;
  con->fd = 0;
  if (opt_verbose) {
    str_copys(&msg, "end #");
    str_catu(&msg, con->number);
    str_cats(&msg, " bytes: ");
    str_catu(&msg, con->total);
    str_cats(&msg, " records: ");
    str_catu(&msg, con->records);
    str_cats(&msg, con->ok ? " OK" : " aborted");
    msg1(msg.s);
  }
  log_status();
}

static void open_connection(connection* con, int fd)
{
  memset(con, 0, sizeof(connection));
  con->fd = fd;
  con->number = connection_number++;
  if (opt_verbose) {
    str_copys(&msg, "start #");
    str_catu(&msg, con->number);
    msg1(msg.s);
  }
}

void die(const char* msg)
{
  error2sys(msg, " failed");
  unlink(opt_socket);
  exit(1);
}

static void use_uid(const char* str)
{
  char* ptr;
  if (!str) usage(1, "UID not found in environment");
  opt_uid = strtoul(str, &ptr, 10);
  if (*ptr != 0) usage(1, "Invalid UID number");
}

static void use_gid(const char* str)
{
  char* ptr;
  if (!str) usage(1, "GID not found in environment");
  opt_gid = strtoul(str, &ptr, 10);
  if (*ptr != 0) usage(1, "Invalid GID number");
}

static void parse_options(char* argv[])
{
  char* ptr;
  if (opt_umask_str) {
    opt_umask = strtoul(opt_umask_str, &ptr, 8);
    if (*ptr != 0) usage(1, "Invalid umask value");
  }
  if (opt_mode_str) {
    opt_mode = strtoul(opt_mode_str, &ptr, 8);
    if (*ptr != 0) usage(1, "Invalid mode value");
  }
  if (opt_timeout >= 1000000) usage(1, "Timeout is too large");
  if (opt_envuidgid) {
    use_gid(getenv("GID"));
    use_uid(getenv("UID"));
  }
  opt_socket = argv[0];
  if (!writer_select(opt_writer)) usage(1, "Invalid writer name");
}

static void nonblock(int fd)
{
  int flags;
  if ((flags = fcntl(fd, F_GETFL)) == -1) die ("fcntl");
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) die("fcntl");
}

static int make_socket(void)
{
  struct sockaddr_un* saddr;
  int s;
  mode_t old_umask;
  
  old_umask = umask(opt_umask);
  saddr = (struct sockaddr_un*)malloc(sizeof(struct sockaddr_un) +
				      strlen(opt_socket) + 1);
  saddr->sun_family = AF_UNIX;
  strcpy(saddr->sun_path, opt_socket);
  unlink(opt_socket);
  s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) die("socket");
  if (bind(s, (struct sockaddr*)saddr, SUN_LEN(saddr)) != 0) die("bind");
  if (opt_mode_str)
    if (chmod(opt_socket, opt_mode) != 0) die("chmod");
  if (listen(s, opt_backlog) != 0) die("listen");
  if (opt_gid != (gid_t)-1 && setgid(opt_gid) == -1) die("setgid");
  if (opt_uid != (uid_t)-1 && setuid(opt_uid) == -1) die("setuid");
  umask(old_umask);
  nonblock(s);
  return s;
}

static int needs_sync;

static void do_sync(void) 
{
  int ok;
  connection* last_connection;
  connection* con;
  static char buf[1];

  last_connection = connections + opt_connections;
  ok = sync_records();
  for (con = connections; con < last_connection; con++) {
    if (con->fd && con->state == -1) {
      buf[0] = con->ok && ok;
      write(con->fd, buf, 1);
      close_connection(con);
    }
  }
}

static void handle_connection(connection* con)
{
  static char buf[4096];
  unsigned long rd;
  rd = read(con->fd, buf, sizeof buf);
  if (!rd || rd == (unsigned long)-1) {
    write_record(con, 0, 1);
    con->state = -1;
  }
  else
    handle_data(con, buf, rd);
  if(con->state == -1) {
    if (con->ok)
      if (connection_count == 1)
	do_sync();
      else
	needs_sync = 1;
    else
      close_connection(con);
  }
}

static void accept_connection(int s)
{
  int fd;
  unsigned i;
  
  do {
    fd = accept(s, NULL, NULL);
    // All the listed error return values are not possible except for
    // buggy code, so just try again if accept fails.
  } while(fd < 0);
  nonblock(fd);
  ++connection_count;
  log_status();

  for (i = 0; i < opt_connections; i++) {
    if (!connections[i].fd) {
      open_connection(connections+i, fd);
      return;
    }
  }
}

static void do_select(int s)
{
  static fd_set rfds;
  int fdmax;
  int fd;
  unsigned i;
  static struct timeval timeout = {0,0};
  static struct timeval sync_time;
  struct timeval* timeptr;

  /* If a sync point is needed, determine the end time when the sync
     will actually happen. */
  if (needs_sync) {
    timeptr = &timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = opt_timeout;
    if (sync_time.tv_sec == 0) {
      gettimeofday(&sync_time, 0);
      sync_time.tv_usec += opt_timeout;
      if (sync_time.tv_usec >= 1000000) {
	sync_time.tv_usec -= 1000000;
	sync_time.tv_sec--;
      }
    }
  }
  else
    timeptr = 0;

  fdmax = -1;
  FD_ZERO(&rfds);
  if (connection_count < opt_connections) {
    FD_SET(s, &rfds);
    fdmax = s;
  }
  for (i = 0; i < opt_connections; i++) {
    fd = connections[i].fd;
    if (fd) {
      FD_SET(fd, &rfds);
      if (fd > fdmax) fdmax = fd;
    }
  }
  while ((fd = select(fdmax+1, &rfds, 0, 0, timeptr)) == -1) {
    if (errno != EINTR) die("select");
    if (needs_sync) timeout.tv_usec = opt_timeout;
  }
  if (needs_sync) {
    /* Do the sync if the select either timed out, or if the end time
       has been passed. */
    if (!fd) {
      do_sync();
      needs_sync = sync_time.tv_sec = sync_time.tv_usec = 0;
    }
    else {
      gettimeofday(&timeout, 0);
      if (timeout.tv_sec > sync_time.tv_sec ||
	  timeout.tv_usec > sync_time.tv_usec) {
	do_sync();
	needs_sync = sync_time.tv_sec = sync_time.tv_usec = 0;
      }
    }
  }
  if (fd) {
    if (FD_ISSET(s, &rfds))
      accept_connection(s);
    for (i = 0; i < opt_connections; i++) {
      fd = connections[i].fd;
      if (FD_ISSET(connections[i].fd, &rfds))
	handle_connection(connections+i);
    }
  }
}

static void handle_intr()
{
  if (opt_synconexit)
    rotate_journal();
  if (opt_delete)
    unlink(opt_socket);
  exit(0);
}

int cli_main(int argc, char* argv[])
{
  int s;

  parse_options(argv);
  if ((connections = malloc(sizeof(connection) * opt_connections)) == 0)
    die1(1, "Out of memory");
  signal(SIGINT, handle_intr);
  signal(SIGTERM, handle_intr);
  signal(SIGQUIT, handle_intr);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);
  s = make_socket();
  if (!open_journal(argv[1]))
    die3sys(1, "Could not open the journal file '", argv[1], "'");
  log_status();
  for(;;)
    do_select(s);
  argc = argc;
}
