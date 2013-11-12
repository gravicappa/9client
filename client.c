#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include "9p.h"
#include "9pconn.h"
#include "util.h"

static const char *sockvar = "P9SOCKET";
static const char *root_fid_var = "P9ROOTFID";

static int cmd_root(int argc, char **argv);
static int cmd_walk(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_write(int argc, char **argv);
static int cmd_write_fid(int argc, char **argv);
static int cmd_xwrite(int argc, char **argv);
static int cmd_read(int argc, char **argv);

struct cmd {
  char *s;
  int (*fn)(int argc, char **argv);
  char *help;
} cmds[] = {
  {"root", cmd_root, "— returns root fid"},
  {"walk", cmd_walk, "<path> — prints fid of destination or -1 on error"},
  {"mkdir", cmd_mkdir, "<path>"},
  {"write", cmd_write, "<path>"},
  {"write_fid", cmd_write_fid, "<fid>"},
  {"xwrite", cmd_xwrite, "<path> <data>"},
  {"read", cmd_read, "<path>"},
  {0}
};

static int fd = -1;
static int port = 5558;
static char *res = "";
static char *user = "nobody";
static char *host = 0;
static struct p9_conn *conn = 0;

static int
cmd_root(int argc, char **argv)
{
  printf("%d\n", p9_root_fid(conn));
  return 0;
}

static int
cmd_walk(int argc, char **argv)
{
  return 0;
}

static int
cmd_mkdir(int argc, char **argv)
{
  return 0;
}

static int
cmd_write(int argc, char **argv)
{
  return 0;
}

static int
cmd_xwrite(int argc, char **argv)
{
  return 0;
}

static int
cmd_write_fid(int argc, char **argv)
{
  return 0;
}

static int
cmd_read(int argc, char **argv)
{
  return 0;
}

int
process_stdin(struct p9_conn *conn)
{
  static char buf[1024];
  char *args[1024] = {0};
  int nargs, i;

  log_printf(LOG_DBG, ";; process_stdin/\n");
  while (fgets(buf, sizeof(buf), stdin)) {
    nargs = parse_args(buf, NITEMS(args), args);
    if (nargs)
      for (i = 0; cmds[i].s; ++i)
        if (!strcmp(args[0], cmds[i].s) && cmds[i].fn)
          cmds[i].fn(nargs - 1, args + 1);
  }
  return 0;
}

int
process_command(int argc, char **argv)
{
  int i, ret = 1, root_fid = -1;
  char *var;

  for (i = 0; cmds[i].s; ++i)
    if (!strcmp(argv[0], cmds[i].s) && cmds[i].fn) {
      conn = mk_p9conn(fd, 0);
      if (!conn)
        die("Cannot create 9P connection");
      if ((var = getenv(root_fid_var)) && sscanf(var, "%d", &root_fid) != 0)
        die("Wrong root fid");
      if (root_fid == -1)
        p9_attach(conn, user, res);
      else
        p9_set_root_fid(root_fid, conn);
      ret = cmds[i].fn(argc - 1, argv + 1);
      rm_p9conn(conn);
      break;
    }
  return ret;
}

int
connect_to(char *host, int port)
{
  struct sockaddr_in addr;
  struct hostent *hostent;
  int fd, flags;

  hostent = gethostbyname(host);
  if (!hostent || (hostent->h_addrtype != AF_INET)) {
    fprintf(stderr, "Cannot resolve host address");
    return -1;
  }
  memcpy(&addr.sin_addr, hostent->h_addr_list[0], hostent->h_length);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
    close(fd);
    return -1;
  }
  if (0) {
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      fprintf(stderr, "Cannot make socket non-blocking\n");
      close(fd);
      return -1;
    }
  }
  return fd;
}

int
init_connection(char *host, int argc, char **argv)
{
  char buf[16];
  struct p9_conn *conn;

  fd = connect_to(host, port);
  if (fd < 0)
    die("Cannot connect to host");

  snprintf(buf, sizeof(buf), "%d", fd);
  if (setenv(sockvar, buf, 1)) {
    close(fd);
    die("Cannot set socket env variable");
  }
  log_printf(LOG_DBG, ";; init_connection/ fd: %d\n", fd);
  conn = mk_p9conn(fd, 1);
  log_printf(LOG_DBG, ";; init_connection/ conn: %p\n", conn);
  if (!conn || p9_attach(conn, user, res) == P9_NOTAG)
    die("Cannot init 9P connection");
  log_printf(LOG_DBG, ";; init_connection/ argc: %d\n", argc);
  if (argc == 0) {
    process_stdin(conn);
    rm_p9conn(conn);
  } else {
    rm_p9conn(conn);
    execvp(argv[0], argv + 1);
    perror("exec");
  }
  return 0;
}

int
main(int argc, char **argv)
{
  int i, ret = 1;
  char *usage = "usage: 9client [-r resource] [-p port] [-a address]"
                " [runcmd ...]\n"
                "       9client [-s fd] filecmd...\n";
  char *sockdef;
  
  logmask = 0xff;
  for (i = 1; i < argc && argv[i][0] == '-'; ++i)
    if (!strcmp(argv[i], "-p") && i + 1 < argc)
      port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-r") && i + 1 < argc)
      res = argv[++i];
    else if (!strcmp(argv[i], "-a") && i + 1 < argc)
      host = argv[++i];
    else if (!strcmp(argv[i], "-s") && i + 1 < argc)
      fd = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-hcmd")) {
      for (i = 0; cmds[i].s; ++i)
        printf("  %s %s\n", cmds[i].s, cmds[i].help ? cmds[i].help : "");
      exit(1);
    }
    else
      die(usage);
  if (fd < 0) {
    sockdef = getenv(sockvar);
    if (sockdef && sscanf(sockdef, "%d", &fd) != 1)
      die("Wrong file descriptor.");
  }
  if (!(user = getenv("USER")))
    user = "nobody";
  if (fd < 0) {
    if (!host)
      die("Host is not specified");
    ret = init_connection(host, argc - i, argv + i);
  } else {
    if (argc == i)
      die("No file command specified");
    ret = process_command(argc - i, argv + i);
  }
  return ret;
}
