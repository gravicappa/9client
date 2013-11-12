struct p9_conn;
struct p9_stat;
typedef void *P9_file;

struct p9_conn *mk_p9conn(int fd, int init);
void rm_p9conn(struct p9_conn *c);

int p9_attach(struct p9_conn *c, char *user, char *res);

unsigned int p9_add_fid(unsigned int fid, struct p9_conn *c);
void p9_rm_fid(unsigned int fid, struct p9_conn *c);
unsigned int p9_root_fid(struct p9_conn *c);
void p9_set_root_fid(unsigned int root_fid, struct p9_conn *c);

int p9fid_walk(unsigned int newfid, unsigned int fid, const char *path,
               struct p9_conn *c);
int p9fid_open(unsigned int fid, int mode, struct p9_conn *c);
void p9fid_close(unsigned int fid, struct p9_conn *c);
int p9fid_create(unsigned int fid, const char *name, int mode, int perm,
                 struct p9_conn *c);

void p9fid_remove(unsigned int fid, struct p9_conn *c);
int p9fid_write(unsigned int fid, uint64_t off, int len, void *data,
                struct p9_conn *c);
int p9fid_read(unsigned int fid, uint64_t off, int len, void *data,
               struct p9_conn *c);
int p9fid_stat(unsigned int fid, struct p9_stat *stat, struct p9_conn *c);

P9_file p9_open(const char *path, int mode, unsigned int root_fid,
                struct p9_conn *c);
P9_file p9_create(const char *path, int mode, int perm, unsigned int root_fid,
                 struct p9_conn *c);
int p9_mkdir(const char *path, int perm, struct p9_conn *c);
void p9_close(P9_file f);
int p9_write(int len, void *data, P9_file f);
int p9_read(int len, void *data, P9_file f);
int p9_readdir(struct p9_stat *entry, P9_file f);
int p9_tell(P9_file f);
int p9_seek(P9_file f, int mode, int seek);

int p9_io_send(struct p9_conn *c, void (*fn)(struct p9_conn *con, void *aux),
               void *aux);
int p9_io_recv(struct p9_conn *c, int wait_tag);
int p9select(int n, int *read_fids, int *write_fids, struct timeval *tv);
