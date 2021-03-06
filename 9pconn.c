#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdio.h>

#include "9p.h"
#include "9pconn.h"
#include "seq.h"
#include "util.h"

#define MSIZE 65536

struct p9_req {
  int tag;
  void *aux;
  void (*fn)(struct p9_conn *c, void *aux);
  struct p9_req *next;
};

struct p9_conn {
  struct p9_connection c;
  int fd;
  int root_fid;
  int outsize;
  int insize;
  int off;
  int logmask;
  unsigned char *outbuf;
  unsigned char *inbuf;
  struct p9_req *req[256];
  struct p9_req *req_pool;
  struct p9_seq *tags;
  struct p9_seq *fids;

  char *user;
  char *res;
};

struct p9_file {
  int fid;
  int off;
  int buf_size;
  int buf_used;
  int buf_off;
  unsigned char *buf;
  struct p9_conn *c;
};

static struct p9_req *
get_req(int tag, struct p9_conn *c)
{
  struct p9_req *r = c->req[tag & 0xff];
  for (r = c->req[tag & 0xff]; r && r->tag != tag; r = r->next) {}
  return r;
}

static int
set_req(struct p9_conn *c, void (*fn)(struct p9_conn *c, void *aux),
        void *aux)
{
  struct p9_req *req;
  int i;
  if (c->req_pool) {
    req = c->req_pool;
    c->req_pool = req->next;
  } else {
    req = calloc(1, sizeof(struct p9_req));
  }
  if (!req)
    return -1;
  req->tag = c->c.t.tag;
  i = req->tag & 0xff;
  req->fn = fn;
  req->aux = aux;
  req->next = c->req[i];
  c->req[i] = req;
  return 0;
}

static unsigned int
unpack_uint4(unsigned char *buf)
{
  return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

int
p9_io_send(struct p9_conn *c, void (*fn)(struct p9_conn *c, void *aux),
           void *aux)
{
  struct p9_msg *m = &c->c.t;
  int size, sent = 0, r;

  if (m->type == P9_TVERSION)
    m->tag = P9_NOTAG;
  else
    m->tag = p9_seq_next(c->tags);
  c->c.r.ename = 0;
  c->c.r.ename_len = 0;
  if (c->logmask)
    p9_print_msg(&c->c.t, "OUT");
  if (p9_pack_msg(c->c.msize, (char *)c->outbuf, m))
    return -1;
  size = unpack_uint4(c->outbuf);
  for (sent = 0; sent < size; ) {
    r = send(c->fd, c->outbuf + sent, size - sent, 0);
    if (r <= 0)
      return -1;
    sent += r;
  }
  set_req(c, fn, aux);
  return m->tag;
}

int
p9_io_recv(struct p9_conn *c, int wait_tag)
{
  int r, off, size, run = 1;
  struct p9_req *req;
  unsigned char *buf = c->inbuf;

  while (run) {
    if (c->insize) {
      memmove(buf, buf + c->insize, c->c.msize - c->insize);
      c->off -= c->insize;
      c->insize = 0;
    }
    r = recv(c->fd, buf + c->off, c->c.msize - c->off, 0);
    if (r == 0)
      return -1;
    if (r < 0)
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    c->insize += r;
    off = c->off;
    while (c->insize - off >= 7) {
      size = unpack_uint4(buf + off);
      if (size < 7)
        return -1;
      if (off + size > c->insize)
        break;
      if (p9_unpack_msg(size, (char *)buf + off, &c->c.r))
        return -1;
      off += size;
      if (c->logmask)
        p9_print_msg(&c->c.r, "IN");
      /* TODO: handle incorrect response type */
      req = get_req(c->c.r.tag, c);
      if (req && req->fn)
        req->fn(c, req->aux);
      p9_seq_drop(c->c.r.tag, c->tags);
      if (c->c.r.tag == wait_tag) {
        run = 0;
        break;
      }
    }
    c->off = off;
  }
  return 1;
}

static int
io_sendrecv(struct p9_conn *c)
{
  int tag, r;
  tag = p9_io_send(c, 0, 0);
  if (tag < 0)
    return -1;
  r = p9_io_recv(c, tag);
  if (r < 0)
    return -1;
  return 0;
}

static int
p9_version(struct p9_conn *c)
{
  c->c.t.type = P9_TVERSION;
  c->c.t.msize = c->c.msize;
  P9_SET_STR(c->c.t.version, "9P2000");
  if (io_sendrecv(c))
    return -1;
  c->c.msize = (c->c.msize < c->c.r.msize) ? c->c.msize : c->c.r.msize;
  if (strcmp(c->c.r.version, "9P2000"))
    return -1;
  c->root_fid = P9_NOFID;
  return 0;
}

int
p9_attach(struct p9_conn *c, char *user, char *res)
{
  c->c.t.type = P9_TATTACH;
  if (c->root_fid != P9_NOFID)
    p9fid_close(c->root_fid, c);
  c->root_fid = p9_seq_next(c->fids);
  c->c.t.fid = c->root_fid;
  c->c.t.afid = P9_NOFID;
  P9_SET_STR(c->c.t.uname, user);
  P9_SET_STR(c->c.t.aname, res);
  if (io_sendrecv(c))
    return P9_NOFID;
  return c->root_fid;
}

struct p9_conn *
mk_p9conn(int fd, int init)
{
  struct p9_conn *c;
  c = calloc(1, sizeof(struct p9_conn));
  if (!c)
    return 0;
  c->c.msize = MSIZE;
  c->fd = fd;
  c->tags = mk_p9seq();
  c->fids = mk_p9seq();
  c->outbuf = malloc(c->c.msize);
  c->inbuf = malloc(c->c.msize);
  c->root_fid = P9_NOFID;
  if (!(c->tags && c->fids && c->outbuf && c->inbuf))
    goto err;
  if (init)
    if (p9_version(c))
      goto err;
  return c;
err:
  rm_p9seq(c->tags);
  rm_p9seq(c->fids);
  if (c->outbuf)
    free(c->outbuf);
  if (c->inbuf)
    free(c->inbuf);
  free(c);
  return 0;
}

void
rm_p9conn(struct p9_conn *c, int clunk_root)
{
  if (!c)
    return;
  if (clunk_root && c->root_fid != P9_NOFID)
    p9fid_close(c->root_fid, c);
  /* TODO: free reqs, req_pool, */
  rm_p9seq(c->tags);
  rm_p9seq(c->fids);
  if (c->outbuf)
    free(c->outbuf);
  if (c->inbuf)
    free(c->inbuf);
  if (c->c.buf)
    free(c->c.buf);
  free(c);
}

unsigned int
p9_root_fid(struct p9_conn *c)
{
  return (c) ? c->root_fid : P9_NOFID;
}

void
p9_set_root_fid(unsigned int root_fid, struct p9_conn *c)
{
  if (c->root_fid != P9_NOFID)
    p9fid_close(c->root_fid, c);
  c->root_fid = root_fid;
}

unsigned int
p9_add_fid(unsigned int fid, struct p9_conn *c)
{
  return p9_seq_add(fid, c->fids);
}

void
p9_rm_fid(unsigned int fid, struct p9_conn *c)
{
  p9_seq_drop(fid, c->fids);
}

int
p9fid_walk(unsigned int newfid, unsigned int fid, const char *path,
           struct p9_conn *c)
{
  struct p9_msg *r = &c->c.r, *m = &c->c.t;
  int i = 0, len, n, off;
  char *p;

  m->type = P9_TWALK;
  m->fid = fid;
  m->newfid = newfid;
  for (; path[i] == '/'; ++i) {}
  off = i;
  do {
    for (n = 0; n < P9_MAXWELEM; ++i) {
      if (path[i] == '/' || path[i] == 0) {
        len = i - off;
        if (len) {
          m->wname[n] = (char *)path + off;
          m->wname_len[n] = len;
          ++n;
        }
        for (; path[i] == '/'; ++i) {}
        off = i;
        if (!path[i])
          break;
      }
    }
    m->nwname = n;
    if (io_sendrecv(c))
      return -1;
    if (r->ename)
      return -1;
    if (r->nwqid < n) {
      p = m->wname[r->nwqid - 1] + m->wname_len[r->nwqid - 1];
      for (; *p == '/'; ++p) {}
      return p - path;
    }
    m->fid = newfid;
  } while (path[i]);
  return i;
}

int
p9fid_open(unsigned int fid, int mode, struct p9_conn *c)
{
  struct p9_msg *m = &c->c.t;
  m->type = P9_TOPEN;
  m->fid = fid;
  m->mode = mode;
  if (io_sendrecv(c))
    return -1;
  return 0;
}

void
p9fid_close(unsigned int fid, struct p9_conn *c)
{
  if (fid == P9_NOFID)
    return;
  c->c.t.type = P9_TCLUNK;
  c->c.t.fid = fid;
  io_sendrecv(c);
  p9_seq_drop(fid, c->fids);
}

int
p9fid_create(unsigned int fid, const char *name, int mode, int perm,
             struct p9_conn *c)
{
  struct p9_msg *m = &c->c.t;
  if (!name)
    return 0;
  m->type = P9_TCREATE;
  m->fid = fid;
  P9_SET_STR(m->name, (char *)name);
  m->perm = perm;
  m->mode = mode;
  if (io_sendrecv(c) || c->c.r.ename)
    return -1;
  return 0;
}

void
p9fid_remove(unsigned int fid, struct p9_conn *c)
{
  c->c.t.type = P9_TREMOVE;
  c->c.t.fid = fid;
  io_sendrecv(c);
}

int
p9fid_write(unsigned int fid, uint64_t off, int len, void *data,
            struct p9_conn *c)
{
  struct p9_msg *m = &c->c.t;
  m->type = P9_TWRITE;
  m->fid = fid;
  m->offset = off;
  m->count = len;
  m->data = data;
  if (io_sendrecv(c) || c->c.r.ename)
    return -1;
  return c->c.r.count;
}

int
p9fid_read(unsigned int fid, uint64_t off, int len, void *data,
           struct p9_conn *c)
{
  struct p9_msg *m = &c->c.t;
  m->type = P9_TREAD;
  m->fid = fid;
  m->offset = off;
  m->count = len;
  if (io_sendrecv(c) || c->c.r.ename)
    return -1;
  len = (len < c->c.r.count) ? len : c->c.r.count;
  memcpy(data, c->c.r.data, len);
  return len;
}

int
p9fid_stat(unsigned int fid, struct p9_stat *stat, struct p9_conn *c)
{
  struct p9_msg *m = &c->c.t;
  m->type = P9_TSTAT;
  m->fid = fid;
  if (io_sendrecv(c) || c->c.r.ename)
    return -1;
  memcpy(stat, &c->c.r.stat, sizeof(c->c.r.stat));
  return 0;
}

int
p9fid_walk2(const char *path, unsigned int fid, struct p9_conn *c,
            unsigned int *newfid)
{
  int r;
  unsigned int f;

  if (fid == P9_NOFID || fid == -1)
    fid = c->root_fid;

  *newfid = P9_NOFID;
  f = p9_seq_next(c->fids);
  r = p9fid_walk(f, fid, path, c);
  if (r >= 0) {
    if (path[r])
      p9_seq_drop(f, c->fids);
    else
      *newfid = f;
  }
  return r;
}


P9_file
p9_open(const char *path, int mode, unsigned int root_fid, struct p9_conn *c)
{
  struct p9_file *f;
  unsigned int fid;
  int r;

  r = p9fid_walk2(path, root_fid, c, &fid);
  if (r < 0 || fid == P9_NOFID)
    return 0;
  if (p9fid_open(fid, mode, c))
    goto err;
  f = calloc(1, sizeof(struct p9_file));
  if (f) {
    f->fid = fid;
    f->c = c;
  }
  return (P9_file)f;
err:
  p9fid_close(fid, c);
  return 0;
}

P9_file
p9_create(const char *path, int mode, int perm, unsigned int root_fid,
          struct p9_conn *c)
{
  struct p9_file *f;
  unsigned int fid;
  int r;
  char *dir = 0;

  r = p9fid_walk2(path, root_fid, c, &fid);
  if (r < 0 || fid != P9_NOFID)
    goto err;
  dir = malloc(r + 1);
  if (!dir)
    return 0;
  memcpy(dir, path, r);
  dir[r] = 0;
  r = p9fid_walk2(dir, root_fid, c, &fid);
  free(dir);
  if (fid == P9_NOFID)
    return 0;
  if (p9fid_create(fid, path + r, mode, perm, c))
    goto err;
  f = calloc(1, sizeof(struct p9_file));
  if (f) {
    f->fid = fid;
    f->c = c;
  }
  return (P9_file) f;
err:
  p9fid_close(fid, c);
  return 0;
}

int
p9_mkdir(const char *path, int perm, struct p9_conn *c)
{
  P9_file dir;

  dir = p9_create(path, 0, P9_DMDIR | perm, P9_NOFID, c);
  if (!dir)
    return -1;
  p9_close(dir);
  return 0;
}

void
p9_close(P9_file file)
{
  struct p9_file *f = file;
  if (f) {
    if (f->buf)
      free(f->buf);
    p9fid_close(f->fid, f->c);
    free(f);
  }
}

int
p9_write(int len, void *data, P9_file file)
{
  struct p9_file *f = file;
  int r;
  if (!f)
    return -1;
  r = p9fid_write(f->fid, f->off, len, data, f->c);
  if (r < 0)
    return -1;
  f->off += r;
  return r;
}

int
p9_read(int len, void *data, P9_file file)
{
  struct p9_file *f = file;
  int r;
  if (!f)
    return -1;
  r = p9fid_read(f->fid, f->off, len, data, f->c);
  if (r < 0)
    return -1;
  f->off += r;
  return r;
}

static int
p9_readstat(struct p9_stat *entry, struct p9_file *f)
{
  unsigned int size;
  size = f->buf[0] | (f->buf[1] << 8);
  if (size + 2 > f->buf_used)
    return 1;
  if (p9_unpack_stat(size + 2, (char *)f->buf, entry))
    return -1;
  f->buf_off = size + 2;
  return 0;
}

int
p9_readdir(struct p9_stat *entry, P9_file file)
{
  struct p9_file *f = file;
  int r;
  if (!f)
    return -1;
  if (f->buf) {
    if (f->buf_off) {
      f->buf_used -= f->buf_off;
      memmove(f->buf, f->buf + f->buf_off, f->buf_used);
      f->buf_off = 0;
    }
    switch (p9_readstat(entry, f)) {
    case -1: return -1;
    case 0: return entry->size + 2;
    default: ;
    }
  } else {
    f->buf_size = f->c->c.msize;
    f->buf_used = f->buf_off = 0;
    f->buf = malloc(f->buf_size);
    if (!f->buf)
      return -1;
  }
  r = p9_read(f->buf_size - f->buf_used, f->buf + f->buf_used, f);
  if (r < 0)
    return -1;
  f->buf_used += r;
  switch (p9_readstat(entry, f)) {
  case -1: return -1;
  case 0: return entry->size + 2;
  default: ;
  }
  return 0;
}

int
p9_tell(P9_file file)
{
  struct p9_file *f = file;
  return (f) ? f->off : 0;
}

int
p9_seek(P9_file file, int off, int whence)
{
  struct p9_file *f = file;
  int prev;
  if (!f)
    return -1;
  prev = f->off;
  switch (whence) {
  case SEEK_SET: f->off = off; break;
  case SEEK_CUR: f->off += off; break;
  default: return -1;
  }
  if (f->buf && f->off != prev)
    f->off = prev;
  return f->off;
}
