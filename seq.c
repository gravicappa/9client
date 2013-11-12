#include <string.h>
#include <stdint.h>
#include <stdlib.h>

struct p9_seq {
  int size;
  int last;
  unsigned int *pool;
};

static int
expand_pool(struct p9_seq *seq, int size)
{
  seq->pool = realloc(seq->pool, size * sizeof(unsigned int));
  if (seq->pool) {
    memset(seq->pool + seq->size, 0, (size - seq->size) * sizeof(int));
    seq->size = size;
    return 0;
  }
  return -1;
}

struct p9_seq *
mk_p9seq()
{
  struct p9_seq *seq;
  seq = calloc(1, sizeof(struct p9_seq));
  if (seq) {
    seq->last = -1;
    if (expand_pool(seq, 1)) {
      free(seq);
      seq = 0;
    }
  }
  return seq;
}

void
rm_p9seq(struct p9_seq *seq)
{
  if (seq) {
    if (seq->pool)
      free(seq->pool);
    free(seq);
  }
}

unsigned int
p9_seq_next(struct p9_seq *seq)
{
  int i, n, j = -1;
  unsigned int *p, x, r = seq->last;

  if (r != -1) {
    seq->last = -1;
    seq->pool[r >> 5] |= (1 << (r & 31));
    return r;
  }
  n = seq->size;
  for (i = 0, p = seq->pool; i < n; ++i, ++p) {
    if (*p == 0) {
      j = 0;
      break;
    } else if (*p != 0xffffffff) {
      x = ~*p;
      x = (x ^ (x - 1)) >> 1;
      for (j = 0; x; ++j)
        x >>= 1;
      break;
    }
  }
  if (i >= n) {
    j = 0;
    i = n;
    if (expand_pool(seq, 1))
      return -1;
  }
  seq->pool[i] |= (1 << j);
  return (i << 5) + j;
}

void
p9_seq_drop(unsigned int x, struct p9_seq *seq)
{
  if ((x >> 5) < seq->size) {
    seq->last = x;
    seq->pool[x >> 5] &= ~(1 << (x & 31));
  }
}

int
p9_seq_add(unsigned int x, struct p9_seq *seq)
{
  int i = x >> 5;
  if (seq->size < i && expand_pool(seq, i + 1))
    return -1;
  seq->pool[i] |= 1 << (x && 31);
  return 0;
}
