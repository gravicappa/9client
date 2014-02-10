#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <signal.h>

#include "9p.h"
#include "9pconn.h"
#include "util.h"

int logmask;
enum {
  MODE_INT = 0,
  MODE_CMD
} mode = 0;

static const char *sockvar = "P9SOCKET";
static const char *root_fid_var = "P9ROOTFID";

static int cmd_root(int argc, char **argv);
static int cmd_walk(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_write(int argc, char **argv);
static int cmd_write_fid(int argc, char **argv);
static int cmd_read(int argc, char **argv);
static int cmd_ls(int argc, char **argv);
static int cmd_quit(int argc, char **argv);

static char buffer[4096];

struct cmd {
  char *s;
  int (*fn)(int argc, char **argv);
  char *help;
} cmds[] = {
  {"write_fid", cmd_write_fid, "<fid> <n>\\n<n bytes of data>"},
  {"write", cmd_write, "<path> <n>\\n<n bytes of data>"},
  {"read", cmd_read, "<path>"},
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

struct cmd shortcuts[] = {
  ['>'] = {0, cmd_write_fid},
  ['<'] = {0, cmd_read},
};

static int fd = -1;
static int port = 5558;
static char *res = "";
static char *user = "nobody";
static char *host = 0;
static struct p9_conn *conn = 0;
static int running;

void
die(char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  exit(1);
}

static void
print_buf(int n, char *buf, int single)
{
  if (mode == MODE_INT)
    printf("%d\n", n);
  if (n)
    fwrite(buf, 1, n, stdout);
  if ((!n || buf[n - 1] != '\n') && (single || mode == MODE_INT))
    puts("");
}

static int
cmd_root(int argc, char **argv)
{
  char buf[10];
  int n;
  n = snprintf(buf, sizeof(buf), "%d", p9_root_fid(conn));
  print_buf(n, buf, 1);
  return 0;
}

static int
cmd_walk(int argc, char **argv)
{
  unsigned int fid, tfid, n;
  char buf[10];
  if (argc < 2)
    goto err;
  fid = p9_root_fid(conn);
  if (p9fid_walk2(argv[1], fid, conn, &tfid) < 0 || tfid == P9_NOFID)
    goto err;
  n = snprintf(buf, sizeof(buf), "%d", tfid);
  print_buf(n, buf, 1);
  return 0;
err:
  puts("err");
  return -1;
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
  int n, size, w, rsize, written = 0;

  switch (mode) {
  case MODE_INT:
    if ((argc < 3 || sscanf(argv[2], "%d", &size) != 1))
      goto err;
    break;
  case MODE_CMD:
    if (argc < 2)
      goto err;
    break;
  }
  f = p9_open(argv[1], P9_OWRITE, -1, conn);
  if (!f)
    goto err;
  switch (mode) {
  case MODE_INT:
    rsize = (sizeof(buffer) < size) ? sizeof(buffer) : size;
    while (written < size) {
      n = fread(buffer, 1, rsize, stdin);
      w = p9_write(n, buffer, f);
      if (w < 0)
        break;
      written += w;
    }
    break;
  case MODE_CMD:
    for (;;) {
      n = fread(buffer, 1, sizeof(buffer), stdin);
      if (n <= 0)
        break;
      w = p9_write(n, buffer, f);
      if (w < 0)
        break;
      written += w;
    }
    break;
  }
  n = snprintf(buffer, sizeof(buffer), "%d", written);
  print_buf(n, buffer, 1);
  p9_close(f);
  return 0;
err:
  puts("err");
  return -1;
}

static int
cmd_write_fid(int argc, char **argv)
{
  unsigned int fid = P9_NOFID, tfid, n, size, rsize, written = 0;

  if (argc < 3 || sscanf(argv[1], "%u", &fid) != 1
      || sscanf(argv[2], "%u", &size) != 1)
    goto err;
  if (p9fid_walk2("", fid, conn, &tfid) != 0 || tfid == P9_NOFID)
    goto err;
  if (p9fid_open(tfid, P9_OWRITE, conn))
    goto err;
  rsize = (sizeof(buffer) < size) ? sizeof(buffer) : size;
  while (written < size) {
    n = fread(buffer, 1, rsize, stdin);
    n = p9fid_write(tfid, written, n, buffer, conn);
    if (n < 0)
      goto err;
    written += n;
  }
  p9fid_close(tfid, conn);
  n = snprintf(buffer, sizeof(buffer), "%d", written);
  print_buf(n, buffer, 1);
  return 0;
err:
  if (tfid != P9_NOFID)
    p9fid_close(tfid, conn);
  puts("err");
  return -1;
}

static int
cmd_read(int argc, char **argv)
{
  P9_file *f;
  char buf[1024];
  int n, ret = 0;

  if (argc < 2)
    goto err;
  f = p9_open(argv[1], P9_OREAD, -1, conn);
  if (!f)
    goto err;

  switch (mode) {
  case MODE_INT:
    while ((n = p9_read(sizeof(buf), buf, f)) > 0)
      print_buf(n, buf, 0);
    print_buf(0, 0, 0);
    break;
  case MODE_CMD:
    n = p9_read(sizeof(buf), buf, f);
    if (n >= 0)
      print_buf(n, buf, 1);
    else {
      fprintf(stderr, "Error reading '%s'\n", argv[1]);
      ret = -1;
    }
    break;
  }
  p9_close(f);
  return ret;
err:
  if (mode == MODE_INT)
    puts("err");
  return -1;
}

static const char *str_from_mode(unsigned int mode)
{
  static char buf[16];
  int u = (mode >> 6) & 7, g = (mode >> 3) & 7, a = mode & 7;
#define R(x) ((x & 4) ? 'r' : '-')
#define W(x) ((x & 2) ? 'w' : '-')
#define X(x) ((x & 1) ? 'x' : '-')
  snprintf(buf, sizeof(buf), "%c%c%c%c%c%c%c%c%c%c",
           (mode & P9_DMDIR) ? 'd' : '-',
           R(u), W(u), X(u), R(g), W(g), X(g), R(a), W(a), X(a));
#undef W
#undef R
#undef X
  return buf;
}

static int
cmd_ls(int argc, char **argv)
{
  P9_file *f;
  struct p9_stat stat;
  char buf[1024], line[256];
  int n, size = 0;

  f = p9_open((argc > 1) ? argv[1] : "/", P9_OREAD, -1, conn);
  if (!f) {
    puts("err");
    return -1;
  }
  while (p9_readdir(&stat, f) > 0) {
    n = snprintf(line, sizeof(line), "%s %8llu %.*s%s\n",
                 str_from_mode(stat.mode), stat.length, stat.name_len,
                 stat.name, ((stat.qid.type & P9_QTDIR) ? "/" : ""));
    if (size + n > sizeof(buf)) {
      print_buf(size, buf, 0);
      size = 0;
    }
    memcpy(buf + size, line, n);
    size += n;
  }
  if (size)
    print_buf(size, buf, 0);
  print_buf(0, 0, 0);
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
run_cmd(int argc, char **argv)
{
  int i, r, c;
  struct cmd *cmd = 0;
  if (argc) {
    c = argv[0][0];
    if (!argv[0][1] && shortcuts[c].fn)
      cmd = &shortcuts[c];
    for (i = 0; cmds[i].s && !cmd; ++i)
      if (!strcmp(argv[0], cmds[i].s) && cmds[i].fn)
        cmd = &cmds[i];
    if (cmd) {
      r = cmd->fn(argc, argv);
      fflush(stdout);
      return r;
    }
  }
  return -1;
}

static int
process_stdin(void)
{
  static char buf[1024];
  char *args[1024] = {0};
  int nargs;

  running = 1;
  while (running && fgets(buf, sizeof(buf), stdin)) {
    nargs = parse_args(buf, NITEMS(args), args);
    run_cmd(nargs, args);
  }
  return 0;
}

int
process_command(int argc, char **argv)
{
  int ret = 1;
  unsigned int root_fid = P9_NOFID;
  char *var;

  mode = MODE_CMD;
  conn = mk_p9conn(fd, 0);
  if (!conn)
    die("Cannot create 9P connection");
  if ((var = getenv(root_fid_var)) && sscanf(var, "%d", &root_fid) != 0)
    die("Wrong root fid");
  if (root_fid == P9_NOFID)
    p9_attach(conn, user, res);
  else
    p9_set_root_fid(root_fid, conn);
  ret = run_cmd(argc, argv);
  rm_p9conn(conn, 0);
  return ret;
}

int
connect_to(char *host, int port)
{
  struct sockaddr_in addr;
  struct hostent *hostent;
  int fd, x;

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
  x = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &x, sizeof(x));
  if (0) {
    x = fcntl(fd, F_GETFL, 0);
    if (x < 0 || fcntl(fd, F_SETFL, x | O_NONBLOCK) < 0) {
      fprintf(stderr, "Cannot make socket non-blocking\n");
      close(fd);
      return -1;
    }
  }
  return fd;
}

void
init_connection(int fd)
{
  char buf[16];

  snprintf(buf, sizeof(buf), "%d", fd);
  if (setenv(sockvar, buf, 1)) {
    close(fd);
    die("Cannot set socket env variable");
  }
  conn = mk_p9conn(fd, 1);
  if (!conn || p9_attach(conn, user, res) == P9_NOTAG)
    die("Cannot init 9P connection");
}

void
sighandle(int sig)
{
  exit(0);
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
    else if (!strcmp(argv[i], "-u") && i + 1 < argc)
      user = argv[++i];
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
  if (!user && !(user = getenv("USER")))
    user = "nobody";
  signal(SIGHUP, sighandle);
  signal(SIGINT, sighandle);
  signal(SIGQUIT, sighandle);
  signal(SIGKILL, sighandle);
  if (fd < 0) {
    if (!host)
      die("Host is not specified");
    fd = connect_to(host, port);
    if (fd < 0)
      die("Cannot connect to host");
  }
  if (host)
    init_connection(fd);
  if (host && argc > i) {
    rm_p9conn(conn, 0);
    execvp(argv[i], argv + i + 1);
    perror("exec");
  } else if (argc == i) {
    process_stdin();
    rm_p9conn(conn, 1);
  } else
    ret = process_command(argc - i, argv + i);
  return ret;
}
