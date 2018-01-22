#include <stdlib.h>
typedef struct node_s { int k, h; struct node_s *c[2]; } node_t;
#define H(C,N) ((C)->c[N] ? (C)->c[N]->h : 0)
void update(node_t *c) {
    const int l = H(c, 0), r = H(c, 1);
    c->h = l > r ? l + 1 : r + 1;
}
void balance(node_t **c, int i, int e) {
    node_t *t = (*c)->c[i];
    if (H((*c)->c[i], 0) - H((*c)->c[i], 1) == e) {
        (*c)->c[i] = (*c)->c[i]->c[1-i];
        t->c[1-i] = (*c)->c[i]->c[i];
        (*c)->c[i]->c[i] = t;
        update((*c)->c[i]->c[i]);
        update((*c)->c[i]);
    }
    t = *c;
    *c = (*c)->c[i];
    t->c[i] = (*c)->c[1-i];
    (*c)->c[1-i] = t;
    update((*c)->c[1-i]);
    update(*c);
}
void rebalance(node_t **c) {
    node_t *t = 0;
    if (H(*c, 0) - H(*c, 1) == 2) balance(c, 0, -1);
    else if (H(*c, 0) - H(*c, 1) == -2) balance(c, 1, 1);
}
void insert(int k, node_t **c) {
    if (!*c) *c = calloc(sizeof **c, 1), (*c)->k = k, (*c)->h = 1;
    else if (k < (*c)->k) insert(k, &(*c)->c[0]), (*c)->h = (*c)->c[0]->h + 1, rebalance(c);
    else if (k > (*c)->k) insert(k, &(*c)->c[1]), (*c)->h = (*c)->c[1]->h + 1, rebalance(c);
}
int search(int k, node_t *c) {
    return c ? (k < c->k ? search(k, c->c[0]) : (k > c->k ? search(k, c->c[1]) : 1)) : 0;
}
void delete(int k, node_t **c) {
    node_t *t = 0;
    if (!c) return;
    else if (k < (*c)->k) { delete(k, &(*c)->c[0]), update(*c), rebalance(c); return; }
    else if (k > (*c)->k) { delete(k, &(*c)->c[1]), update(*c), rebalance(c); return; }
    if (!(*c)->c[0] && !(*c)->c[1]) free(*c), *c = 0;
    else if (!(*c)->c[0]) t = (*c)->c[1], free(*c), *c = t;
    else if (!(*c)->c[1]) t = (*c)->c[0], free(*c), *c = t;
    else {
        for (t = (*c)->c[0]; t->c[1]; t = t->c[1]);
        delete((k = t->k), &(*c)->c[0]), (*c)->k = k, update(*c), rebalance(c);
    }
}

// Test driver
#include <stdio.h>
void print(node_t *c) {
    if (c) {
        print(c->c[0]);
        printf("%*d\n", c->h, c->k);
        print(c->c[1]);
    }
}

int main() {
    node_t *root = 0;
    insert(1, &root);
    insert(-4, &root);
    insert(10, &root);
    insert(30, &root);
    insert(22, &root);
    insert(6000, &root);
    insert(-400, &root);
    insert(314, &root);
    delete(10, &root);
    delete(-400, &root);
    print(root);
    printf("%d\n", search(314, root));
    printf("%d\n", search(10, root));
}
