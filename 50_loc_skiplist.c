#include <stdlib.h>
#include <limits.h>
#define MAX_LVL 6
typedef struct node_s { int k, v; struct node_s **n; } node_t;
typedef struct { int e, s; node_t *h; } skiplist_t;
void skiplist_init(skiplist_t *l) {
    l->h = malloc(sizeof(node_t));
    l->h->k = INT_MAX, l->h->n = malloc(sizeof(node_t*) * (MAX_LVL+1));
    for (int i = 0; i <= MAX_LVL; i++, l->h->n[i] = l->h);
    l->e = 1, l->s = 0;
}
static int rndlvl(int e) {
    for(e = 1; rand() < RAND_MAX/2 && e < MAX_LVL; e++);
    return e;
}
void skiplist_insert(skiplist_t *l, int k, int v) {
    node_t *update[MAX_LVL+1], *x = l->h;
    for (int i = l->e; i >= 1; i--) {
        for(; x->n[i]->k < k; x = x->n[i]);
        update[i] = x;
    }
    if (k == (x = x->n[1])->k) { x->v = v; return; }
    int e = rndlvl((int){0});
    if (e > l->e) {
        for (int i = l->e+1; i <= e; update[i++] = l->h);
        l->e = e;
    }
    x = malloc(sizeof *x);
    x->k = k, x->v = v, x->n = malloc(sizeof(node_t) * (e + 1));
    for (int i = 1; i <= e; i++, x->n[i] = update[i]->n[i], update[i]->n[i] = x);
}
node_t *skiplist_search(skiplist_t *l, int k) {
    node_t *x = l->h;
    for (int i = l->e; i >= 1; i--)
        for (; x->n[i]->k < k; x = x->n[i]);
    return x->n[1]->k == k ? x->n[1] : NULL;
}
void skiplist_delete(skiplist_t *l, int k) {
    node_t *update[MAX_LVL + 1], *x = l->h;
    for (int i = l->e; i >= 1; i--) {
        for (; x->n[i]->k < k; x = x->n[i]);
        update[i] = x;
    }
    if ((x = x->n[1])->k != k) return;
    for (int i = 1; i <= l->e; i++) {
        if (update[i]->n[i] != x) break;
        update[i]->n[1] = x->n[i];
    }
    if (x) free(x->n); free(x);
    for (; l->e > 1 && l->h->n[l->e] == l->h; l->e--);
}
