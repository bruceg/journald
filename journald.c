#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include "journald.h"

extern void setup_env(int, const char*);

#ifndef SUN_LEN
/* Evaluate to actual length of the `sockaddr_un' structure.  */
#define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)	      \
		      + strlen ((ptr)->sun_path))
#endif

static const char* argv0;

static unsigned connection_count = 0;

static unsigned opt_quiet = 0;
static unsigned opt_verbose = 0;
static unsigned opt_delete = 1;
static unsigned opt_connections = 10;
static const char* opt_socket;
static uid_t opt_uid = -1;
static gid_t opt_gid = -1;
static mode_t opt_umask = 0;
static int opt_backlog = 128;

int opt_twopass = 0;
unsigned long opt_maxsize = 1000000;

static connection* connections;

static const char* usage_str =
"usage: %s [options] socket journal-dir\n"
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
"  -x N         Maximum journal file size, in bytes. (default 1000000)\n";

void usage(const char* message)
{
  if (message)
    fprintf(stderr, "%s: %s\n", argv0, message);
  fprintf(stderr, usage_str, argv0);
  exit(1);
}

void log_status(void)
{
  if (opt_verbose)
    printf("journald: status: %d/%d\n", connection_count, opt_connections);
}

void log_connection_exit(int slot)
{
  if (opt_verbose)
    printf("journald: end %d bytes %ld records %ld %s\n", slot,
	   connections[slot].total, connections[slot].records,
	   connections[slot].ok ? "OK" : "Aborted");
  log_status();
}

void log_connection_start(int slot)
{
  if (opt_verbose)
    printf("journald: start %d\n", slot);
}

void die(const char* msg)
{
  perror(msg);
  unlink(opt_socket);
  exit(1);
}

void use_uid(const char* str)
{
  char* ptr;
  if (!str) usage("UID not found in environment.");
  opt_uid = strtoul(str, &ptr, 10);
  if (*ptr != 0) usage("Invalid UID number");
}

void use_gid(const char* str)
{
  char* ptr;
  if (!str) usage("GID not found in environment.");
  opt_gid = strtoul(str, &ptr, 10);
  if (*ptr != 0) usage("Invalid GID number");
}

void parse_options(int argc, char* argv[])
{
  int opt;
  char* ptr;
  argv0 = argv[0];
  while((opt = getopt(argc, argv, "12qQvc:u:g:Ub:B:m:x:")) != EOF) {
    switch(opt) {
    case '1': opt_twopass = 0; break;
    case '2': opt_twopass = 1; break;
    case 'x':
      opt_maxsize = strtoul(optarg, &ptr, 10);
      if (*ptr != 0) usage("Invalid maximum size.");
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
  if (chdir(argv[1])) usage("Could not change directory.");
}

static void nonblock(int fd)
{
  int flags;
  if ((flags = fcntl(fd, F_GETFL)) == -1) die ("fcntl");
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) die("fcntl");
}

int make_socket()
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

void handle_connection(connection* con)
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
    if (con->ok) {
      if (!sync_records()) con->ok = 0;
      buf[0] = con->ok;
      write(con->fd, buf, 1);
    }
    close(con->fd);
    --connection_count;
    log_connection_exit(con-connections);
    con->fd = 0;
  }
}

void accept_connection(int s)
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
      memset(connections+i, 0, sizeof(connection));
      connections[i].fd = fd;
      log_connection_start(i);
      return;
    }
  }
}

void do_select(int s)
{
  static fd_set rfds;
  int fdmax;
  int fd;
  int i;
  
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
  while ((fd = select(fdmax+1, &rfds, 0, 0, 0)) == -1) {
    if (errno == EINTR) continue;
    die("select");
  }
  if (FD_ISSET(s, &rfds))
    accept_connection(s);
  for (i = 0; i < opt_connections; i++) {
    fd = connections[i].fd;
    if (FD_ISSET(connections[i].fd, &rfds))
      handle_connection(connections+i);
  }
}

void handle_intr()
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
  if (!open_journal()) return 1;
  s = make_socket();
  log_status();
  for(;;)
    do_select(s);
}
