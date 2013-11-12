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
static int cmd_ls(int argc, char **argv);
static int cmd_quit(int argc, char **argv);

struct cmd {
  char *s;
  int (*fn)(int argc, char **argv);
  char *help;
} cmds[] = {
  {"read", cmd_read, "<path>"},
  {"write", cmd_write, "<path> <n>\\n<n bytes of data>"},
  {"write_fid", cmd_write_fid, "<fid> <n>\\n<n bytes of data>"},
  {"xwrite", cmd_xwrite, "<path> <data>"},
  {"walk", cmd_walk, "<path> — prints fid of destination or -1 on error"},
  {"mkdir", cmd_mkdir, "<path>"},
  {"root", cmd_root, "— returns root fid"},
  {"ls", cmd_ls, "<path>"},
  {"quit", cmd_quit},
  {"exit", cmd_quit},
  {"q", cmd_quit},
  {"x", cmd_quit},
  {0}
};

static int fd = -1;
static int port = 5558;
static char *res = "";
static char *user = "nobody";
static char *host = 0;
static struct p9_conn *conn = 0;
static int running;

static int
cmd_root(int argc, char **argv)
{
  char buf[10];
  int n;
  n = snprintf(buf, sizeof(buf), "%d", p9_root_fid(conn));
  printf("%d:%s\n", n, buf);
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
  int perm = 0700;

  if (argc < 2)
    goto err;
  if (argc > 2 && sscanf(argv[3], "%o", &perm) != 1)
    goto err;
  if (p9_mkdir(argv[1], perm, conn))
    goto err;
  puts("ok");
  return 0;
err:
  puts("err");
  return -1;
}

static int
cmd_write(int argc, char **argv)
{
  P9_file *f;
  int n, size, w, written = 0;
  char buf[1024];
  if (argc < 3 || sscanf(argv[2], "%d", &size) != 1)
    goto err;
  f = p9_open(argv[1], P9_OWRITE, -1, conn);
  if (!f)
    goto err;
  while (written < size) {
    n = fread(buf, sizeof(buf), 1, stdin);
    w = p9_write(n, buf, f);
    if (w < 0)
      break;
    written += w;
  }
  n = snprintf(buf, sizeof(buf), "%d", written);
  printf("%d:%s\n", n, buf);
  p9_close(f);
  return 0;
err:
  puts("err");
  return -1;
}

static int
cmd_xwrite(int argc, char **argv)
{
  P9_file *f;
  int n;
  char buf[10];

  if (argc < 3)
    goto err;
  f = p9_open(argv[1], P9_OWRITE, -1, conn);
  if (!f)
    goto err;
  n = p9_write(strlen(argv[2]), argv[2], f);
  n = snprintf(buf, sizeof(buf), "%d", n);
  printf("%d:%s\n", n, buf);
  p9_close(f);
  return 0;
err:
  puts("err");
  return -1;
}

static int
cmd_write_fid(int argc, char **argv)
{
  goto err;
  if (argc < 2)
    goto err;
err:
  puts("err");
  return -1;
}

static int
cmd_read(int argc, char **argv)
{
  P9_file *f;
  char buf[1024];
  int n;
  if (argc < 2)
    goto err;
  f = p9_open(argv[1], P9_OREAD, -1, conn);
  if (!f)
    goto err;
  while ((n = p9_read(sizeof(buf), buf, f)) > 0) {
    printf("%d:", n);
    fwrite(buf, 1, n, stdout);
    puts("");
  }
  p9_close(f);
  return 0;
err:
  puts("err");
  return -1;
}

static int
cmd_ls(int argc, char **argv)
{
  P9_file *f;
  struct p9_stat stat;
  char buf[1024];
  int n;

  f = p9_open((argc > 1) ? argv[1] : "/", P9_OREAD, -1, conn);
  if (!f) {
    puts("err");
    return -1;
  }
  while (p9_readdir(&stat, f) > 0) {
    n = snprintf(buf, sizeof(buf), "%x\t%llu\t%.*s%s",
                 stat.mode, stat.length, stat.name_len, stat.name,
                 ((stat.qid.type & P9_QTDIR) ? "/" : ""));
    printf("%d:%s\n", n, buf);
  }
  puts("0:\n");
  p9_close(f);
  return 0;
}

static int
cmd_quit(int argc, char **argv)
{
  running = 0;
  return 0;
}

static int
process_stdin(void)
{
  static char buf[1024];
  char *args[1024] = {0};
  int nargs, i;

  printf("root: %u\n", p9_root_fid(conn));
  log_printf(LOG_DBG, "; process_stdin/\n");
  running = 1;
  while (running && fgets(buf, sizeof(buf), stdin)) {
    nargs = parse_args(buf, NITEMS(args), args);
    if (nargs)
      for (i = 0; cmds[i].s; ++i)
        if (!strcmp(args[0], cmds[i].s) && cmds[i].fn)
          cmds[i].fn(nargs, args);
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
      ret = cmds[i].fn(argc, argv);
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

  fd = connect_to(host, port);
  if (fd < 0)
    die("Cannot connect to host");

  snprintf(buf, sizeof(buf), "%d", fd);
  if (setenv(sockvar, buf, 1)) {
    close(fd);
    die("Cannot set socket env variable");
  }
  log_printf(LOG_DBG, "; init_connection/ fd: %d\n", fd);
  conn = mk_p9conn(fd, 1);
  log_printf(LOG_DBG, "; init_connection/ conn: %p\n", conn);
  if (!conn || p9_attach(conn, user, res) == P9_NOTAG)
    die("Cannot init 9P connection");
  log_printf(LOG_DBG, "; init_connection/ argc: %d\n", argc);
  if (argc == 0) {
    process_stdin();
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
  
  logmask = 0xff & ~LOG_MSG;
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
