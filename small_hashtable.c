#include <string.h>
#include <stdlib.h>
#define SIZE 1024
static int (**hnew())[2] {
    return memset(malloc(sizeof(int**) * SIZE), 0, sizeof(int**) * SIZE);
}
static void hdel(int (**e)[2]) {
    for (size_t i = 0; i < SIZE; i++) free(e[i]); free(e);
}
static int (**hget(int (**tab)[2], int key))[2] {
    int h = key & (SIZE - 1);
    while (tab[h] && **tab[h] != key) h = ((h + 1) & (SIZE - 1));
    return &tab[h];
}
static void hset(int (**tab)[2], int key, int val) {
    *hget(tab, key) = memcpy(malloc(sizeof(int[2])), (int[2]){key,val}, sizeof(int[2]));
}

// TEST
#include <stdio.h>
int main() {
    int (**table)[2] = hnew();

    hset(table, 10, 20);
    hset(table, 20, 30);
    hset(table, 30, 40);

    int (**a)[2] = hget(table, 10);
    int (**b)[2] = hget(table, 20);
    int (**c)[2] = hget(table, 30);

    printf("%d:%d\n", (**a)[0], (**a)[1]);
    printf("%d:%d\n", (**b)[0], (**b)[1]);
    printf("%d:%d\n", (**c)[0], (**c)[1]);

    hdel(table);
}

