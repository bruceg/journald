/* journald.c - Journalling daemon socket handling and main loop.
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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include "journald_server.h"

extern void setup_env(int, const char*);

#ifndef SUN_LEN
/* Evaluate to actual length of the `sockaddr_un' structure.  */
#define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

static const char* argv0;

static unsigned connection_count = 0;
static unsigned long connection_number = 0;

static unsigned long opt_timeout = 10*1000;
static unsigned opt_quiet = 0;
static unsigned opt_verbose = 0;
static unsigned opt_delete = 1;
static const char* opt_socket;
static const char* opt_filename;
static uid_t opt_uid = -1;
static gid_t opt_gid = -1;
static mode_t opt_umask = 0;
static int opt_backlog = 128;

int opt_twopass = 0;
unsigned opt_connections = 10;
connection* connections;

static const char* usage_str =
"usage: %s [options] socket journal-file\n"
"  -u UID       Change user id to UID after creating socket.\n"
"  -g GID       Change group id to GID after creating socket.\n"
"  -U           Same as '-u $UID -g $GID'.\n"
"  -m MASK      Set umask to MASK (in octal) before creating socket.\n"
"               (defaults to 0)\n"
"  -c N         Do not handle more than N simultaneous connections.\n"
"               (default 10)\n"
"  -b N         Allow a backlog of N connections.\n"
"  -1           Single-pass transaction commit (default).\n"
"  -2           Re-write the type flag to commit a transaction.\n"
"               (slower, but guarantees consistency)\n"
"  -t N         Pause synchronization by N us. (default 10ms)\n";

static void usage(const char* message)
{
  if (message)
    fprintf(stderr, "%s: %s\n", argv0, message);
  fprintf(stderr, usage_str, argv0);
  exit(1);
}

static void log_status(void)
{
  if (opt_verbose)
    printf("journald: status: %d/%d\n", connection_count, opt_connections);
}

static void close_connection(connection* con)
{
  close(con->fd);
  --connection_count;
  con->fd = 0;
  if (opt_verbose)
    printf("journald: end #%lu bytes %ld records %ld %s\n", con->number,
	   con->total, con->records, con->ok ? "OK" : "Aborted");
  log_status();
}

static void open_connection(connection* con, int fd)
{
  memset(con, 0, sizeof(connection));
  con->fd = fd;
  con->number = connection_number++;
  if (opt_verbose)
    printf("journald: start #%lu\n", con->number);
}

void die(const char* msg)
{
  perror(msg);
  unlink(opt_socket);
  exit(1);
}

static void use_uid(const char* str)
{
  char* ptr;
  if (!str) usage("UID not found in environment.");
  opt_uid = strtoul(str, &ptr, 10);
  if (*ptr != 0) usage("Invalid UID number");
}

static void use_gid(const char* str)
{
  char* ptr;
  if (!str) usage("GID not found in environment.");
  opt_gid = strtoul(str, &ptr, 10);
  if (*ptr != 0) usage("Invalid GID number");
}

static void parse_options(int argc, char* argv[])
{
  int opt;
  char* ptr;
  argv0 = argv[0];
  while((opt = getopt(argc, argv, "12qQvc:u:g:Ub:B:m:t:")) != EOF) {
    switch(opt) {
    case '1': opt_twopass = 0; break;
    case '2': opt_twopass = 1; break;
    case 't':
      opt_timeout = strtol(optarg, &ptr, 10);
      if (*ptr != 0 || opt_timeout >= 1000000) usage("Invalid timeout.");
      break;
    case 'q': opt_quiet = 1; opt_verbose = 0; break;
    case 'Q': opt_quiet = 0; break;
    case 'v': opt_quiet = 0; opt_verbose = 1; break;
    case 'd': opt_delete = 0; break;
    case 'D': opt_delete = 1; break;
    case 'c':
      opt_connections = strtoul(optarg, &ptr, 10);
      if (*ptr != 0) usage("Invalid connection limit number.");
      break;
    case 'u': use_uid(optarg); break;
    case 'g': use_gid(optarg); break;
    case 'U':
      use_uid(getenv("UID"));
      use_gid(getenv("GID"));
      break;
    case 'm':
      opt_umask = strtoul(optarg, &ptr, 8);
      if (*ptr != 0) usage("Invalid mask value.");
      break;
    case 'b':
      opt_backlog = strtoul(optarg, &ptr, 10);
      if (*ptr != 0) usage("Invalid backlog count.");
      break;
    default:
      usage(0);
    }
  }
  argc -= optind;
  argv += optind;
  if (argc != 2) usage(0);
  opt_socket = argv[0];
  opt_filename = argv[1];
}

static void nonblock(int fd)
{
  int flags;
  if ((flags = fcntl(fd, F_GETFL)) == -1) die ("fcntl");
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) die("fcntl");
}

static int make_socket()
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
      needs_sync = 1;
    else
      close_connection(con);
  }
}

static void accept_connection(int s)
{
  int fd;
  int i;
  
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
  int i;
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
  rotate_journal();
  if (opt_delete)
    unlink(opt_socket);
  exit(0);
}

int main(int argc, char* argv[])
{
  int s;
  parse_options(argc, argv);
  connections = malloc(sizeof(connection) * opt_connections);
  signal(SIGINT, handle_intr);
  signal(SIGTERM, handle_intr);
  signal(SIGQUIT, handle_intr);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);
  s = make_socket();
  if (!open_journal(opt_filename))
    usage("Could not write journal file.");
  log_status();
  for(;;)
    do_select(s);
}
