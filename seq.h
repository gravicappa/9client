struct p9_seq;

struct p9_seq *mk_p9seq();
void rm_p9seq(struct p9_seq *seq);

unsigned int p9_seq_next(struct p9_seq *seq);
int p9_seq_add(unsigned int x, struct p9_seq *seq);
void p9_seq_drop(unsigned int x, struct p9_seq *seq);
