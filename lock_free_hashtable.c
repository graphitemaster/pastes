/*
 * Copyright (C) 2014
 *      Dale Weiler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * This is a lock-free implementation of an extensible hashtable with
 * concurrent insert, delete and find operations with O(1) runtime
 * complexity.
 *
 *  This is based off of:
 *      Split-Ordered Lists: Lock-Free Extensible Hash Tables
 *          Ori Shalev and Nir Shavit
 *      http://www.cs.ucf.edu/~dcm/Teaching/COT4810-Spring2011/Literature/SplitOrderedLists.pdf
 *
 * TL;DR:
 *  With the use of the algorithm described in the paper, one can safely
 *  split elements in a linked-list using a simple single CAS operation
 *  while preserving ordering of the elements. The algorithm described,
 *  recursive split-ordering, works by overcoming the difficulty of
 *  atomically moving items from one bucket to another in the process
 *  of resizing or removing. This is acomplished by using a single
 *  lock-free linked list and assigning the bucket pointers to the places
 *  within the list where a sublist of items that are part of that bucket
 *  can be found.
 *
 *  This sounds trivial in principal, but the list needs to be sorted in
 *  such a way that splitting doesn't distrupt the correct ordering of it.
 *  Similarly, the operation of splitting may be recursive, thus making the
 *  process of ensursing the ordering complex. This is where the split-order
 *  algorithm comes in to play.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

#define HASHTABLE_DEBUG
#define HASHTABLE_LOAD_FACTOR  0.7f
#define HASHTABLE_CALLOC(T, S) memset(malloc((T) * (S)), 0, ((T) * (S)))
#define HASHTABLE_FREE(X)      free(X)

typedef struct hash_node_s  hash_node_t;
typedef struct hash_table_s hash_table_t;

typedef void         *hash_key_t;
typedef void         *hash_value_t;
typedef hash_node_t  *hash_mark_t;
typedef size_t        hash_size_t;
typedef pthread_key_t hashtable_hazard_t;

struct hash_node_s {
    hash_size_t  code;
    hash_key_t   key;
    hash_value_t value;
    hash_mark_t  next;
};

struct hash_table_s {
    hash_mark_t       *table;
    hashtable_hazard_t hazard;
    size_t             count;
    size_t             size;
    bool               lvalue;
};

/* key hashing */
static inline hash_size_t hashtable_rvalue(hash_size_t k) {
/*
 * The following bit twiddling hacks are brought to you by the most obvious
 * webpage on the internet to get bit twiddling hacks.
 *  http://graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith64BitsDiv
 */
#if 1
#   if ULONG_MAX == 0xffffffff
#       define byte(b) ((((b) * 0x0802LU & 0x22110LU) | ((b) * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16)
#   else
#       define byte(b) (((b) * 0x0202020202ULL & 0x010884422010ULL) % 1023)
#   endif
    return (byte((k & 0x000000ff) >> 0)  << 24) |
           (byte((k & 0x0000ff00) >> 8)  << 16) |
           (byte((k & 0x00ff0000) >> 16) << 8)  |
           (byte((k & 0xff000000) >> 24) << 0);
#   undef byte
#else
    /* for reference */
    hash_size_t r = 0;
    for (size_t i = 0; i < 32; ++i)
        r |= ((k & (1 << i)) >> i) << (31 - i);
    return r;
#endif
}

static inline hash_size_t hashtable_hash_key(hash_key_t k) {
    /* golden ratio term: f() = 2^32*Phi */
    return (hash_size_t)((uintptr_t)k * 2654435761u);
}

static inline hash_size_t hashtable_hash_key_regular(hash_size_t k) {
    return hashtable_rvalue(k | 0x80000000);
}

static inline hash_size_t hashtable_hash_key_dummy(hash_size_t k) {
    return hashtable_rvalue(k & 0x7fffffff);
}

/*
 * GCC 4.1.2+ adds builtin intrinsics for atomic memory access. We will
 * utilize those if the exist. Otherwise we'll fall back to inline
 * assembler versions. The same applies for memory barriers.
 */
#if (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) >= 40102
#   define hashtable_atomic_fai(T) __sync_fetch_and_add((T), 1)
#   define hashtable_atomic_fad(T) __sync_fetch_and_sub((T), 1)
#   define hashtable_atomic_cas    __sync_bool_compare_and_swap
#else
static inline int hashtable_atomic_fetch_add(volatile int *x) {
    __asm__( "lock; xadd %0, %1" : "=r"(v), "=n"(*x) : "0"(v) : "memory");
    return v;
}
#   define hashtable_atomic_fai(T) hashtable_atomic_fetch_add((T), 1)
#   define hashtable_atomic_fad(T) hashtable_atomic_fetch_add((T),-1)
#endif

#if (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) >= 40400
#   define hashtable_atomic_lfence() __builtin_ia32_lfence()
#   define hashtable_atomic_sfence() __builtin_ia32_sfence()
#else
#   define hashtable_atomic_lfence() __asm__ __volatile__ ("lfence" ::: "memory")
#   define hashtable_atomic_sfence() __asm__ __volatile__ ("sfence" ::: "memory")
#endif

#define hashtable_atomic_load(P) ({    \
        __typeof__(*(P)) load = *(P);  \
        hashtable_atomic_lfence();     \
        load;                          \
    })

#define hashtable_atomic_store(P, V)   \
    do {                               \
        hashtable_atomic_sfence();     \
        *(P) = (V);                    \
    } while (0)


/* Hashtable bit-node management */
static inline hash_mark_t hashtable_node_make(hash_node_t *node, uintptr_t bit) {
    return (hash_mark_t)(((uintptr_t)node) | bit);
}

static inline hash_node_t *hashtable_node_get(hash_mark_t mark) {
    return (hash_node_t*)(((uintptr_t)mark) & ~(uintptr_t)1);
}

static inline uintptr_t hashtable_node_bit(hash_mark_t mark) {
    return (uintptr_t)mark & 1;
}

static inline void hashtable_node_destroy(hash_mark_t mark) {
    if (hashtable_node_bit(mark) != 0)
        abort();
}

static inline bool hashtable_node_regular(hash_size_t k) {
    return (k & 1) == 1;
}

/*
 * To facilitate in memory reclimation for the lock-free nature of this
 * hashtable there is a few methods, the only patent-free one is the use
 * of hazard pointers.
 *
 * This is a minimal domain-specific implementation of:
 *  Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects
 *      Maged M. Michael
 *  http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.130.8984&rep=rep1&type=pdf
 */
typedef void *hashtable_hazard_ptr_t;

static hashtable_hazard_ptr_t *hashtable_hazard_ptr_table(hashtable_hazard_t *ctx) {
    hashtable_hazard_ptr_t *table = pthread_getspecific(*ctx);
    if (!table) {
        table = HASHTABLE_CALLOC(sizeof(hashtable_hazard_ptr_t), 3);
        pthread_setspecific(*ctx, table);
    }
    return table;
}

/*
 * For the hazard mask tags, and to prevent ABA problems, the tag needs to
 * contain enough bits to make wraparound impossible. We mask off the two
 * bottom bits as such.
 */
static inline hashtable_hazard_ptr_t *hashtable_hazard_ptr_unmask(hashtable_hazard_ptr_t *ptr) {
    return (hashtable_hazard_ptr_t*)((uintptr_t)ptr & ~(uintptr_t)3);
}

static hashtable_hazard_ptr_t hashtable_hazard_ptr_get(
    hashtable_hazard_t     *ctx,
    hashtable_hazard_ptr_t *pointer,
    size_t                  index
) {
    hashtable_hazard_ptr_t result = *pointer;
    hashtable_hazard_ptr_table(ctx)[index] = result;
    return result;
}

static hashtable_hazard_ptr_t hashtable_hazard_ptr_get_with_mask(
    hashtable_hazard_t     *ctx,
    hashtable_hazard_ptr_t *pointer,
    size_t                  index
) {
    hashtable_hazard_ptr_t result = *hashtable_hazard_ptr_unmask(pointer);
    hashtable_hazard_ptr_table(ctx)[index] = hashtable_hazard_ptr_unmask(result);
    return result;
}

static inline void *hashtable_hazard_ptr_get_current(hashtable_hazard_t *ctx, size_t index) {
    return hashtable_hazard_ptr_table(ctx)[index];
}

static inline void hashtable_hazard_ptr_clear(hashtable_hazard_t *ctx, size_t index) {
    hashtable_hazard_ptr_table(ctx)[index] = NULL;
}

static inline void hashtable_hazard_ptr_set(hashtable_hazard_t *ctx, hashtable_hazard_ptr_t p, size_t index) {
    hashtable_hazard_ptr_table(ctx)[index] = p;
}

static inline void hashtable_hazard_ptr_set_with_mask(hashtable_hazard_t *ctx, hashtable_hazard_ptr_t p, size_t index) {
    hashtable_hazard_ptr_table(ctx)[index] = hashtable_hazard_ptr_unmask(p);
}

static inline void hashtable_hazard_ptr_clear_all(hashtable_hazard_t *ctx) {
    hashtable_hazard_ptr_t *table = hashtable_hazard_ptr_table(ctx);
    for (size_t i = 0; i < 3; i++)
        table[i] = NULL;
}

/*
 * Hazard pointer contact:
 *  - On entry hazard pointers shall be available
 *  - On exit hazard pointers will contain
 *      - if (result) [null, null,     prev]
 *      - else        [next, currrent, prev]
 */
static hash_mark_t hashtable_list_find(
    hash_table_t *hashtable,
    size_t        bucket,
    hash_key_t    key,
    hash_size_t   code,
    hash_mark_t **result
) {
    hash_mark_t *table;
    hash_mark_t *head;
    hash_mark_t *prev;
    hash_mark_t  next;
    hash_mark_t  current;

hashtable_list_find_again:
    table   = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&hashtable->table, 0);
    head    = &table[bucket];
    prev    = head;
    current = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)prev, 1);

    for (;;) {
        hash_size_t chash;
        hash_key_t  ckey;

        if (!hashtable_node_get(current))
            goto hashtable_list_find_done;

        next  = hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, (void **)&current->next, 0);
        chash = current->code;
        ckey  = current->key;

        if (hashtable_atomic_load(prev) != hashtable_node_make(hashtable_node_get(current), 0))
            goto hashtable_list_find_again;

        if (!hashtable_node_bit(next)) {
            if (chash > code || (chash == code && ckey == key))
                goto hashtable_list_find_done;

            prev = &hashtable_node_get(current)->next;
            hashtable_hazard_ptr_set_with_mask(&hashtable->hazard, current, 2);
        } else {
            if (hashtable_atomic_cas(
                prev,
                hashtable_node_make(hashtable_node_get(current), 0),
                hashtable_node_make(hashtable_node_get(next),    0)
            ))
                hashtable_node_destroy(hashtable_node_get(current));
            else
                goto hashtable_list_find_again;
        }

        current = next;
        hashtable_hazard_ptr_set_with_mask(&hashtable->hazard, next, 1);
    }
hashtable_list_find_done:
    *result = prev;
    return current;
}

/*
 * Similar to hashtable_list_find except the entire hashtable contents
 * are sweeped.
 *
 * Hazard pointer contract:
 *  - On entry hazard pointers shall be available
 *  - On exit hazard pointers will contain
 *      - if (result) [null, null,     prev]
 *      - else        [next, currrent, prev]
 */
static void hashtable_list_sweep(hash_table_t *hashtable) {
    hash_mark_t *table;
    hash_mark_t *head;
    hash_mark_t *prev;
    hash_mark_t  next;
    hash_mark_t  current;

hashtable_list_sweep_again:
    table   = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&hashtable->table, 0);
    head    = &table[0];
    prev    = head;
    current = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)prev, 1);

    for (;;) {
        if (!hashtable_node_get(current))
            break;

        next = hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, (void **)&current->next, 0);

        if (hashtable_atomic_load(prev) != hashtable_node_make(hashtable_node_get(current), 0))
            goto hashtable_list_sweep_again;

        if (!hashtable_node_bit(next)) {
            prev = &hashtable_node_get(current)->next;
            hashtable_hazard_ptr_set_with_mask(&hashtable->hazard, current, 2);
        } else {
            if (hashtable_atomic_cas(
                prev,
                hashtable_node_make(hashtable_node_get(current), 0),
                hashtable_node_make(hashtable_node_get(next),    0)
            ))
                hashtable_node_destroy(hashtable_node_get(current));
            else
                goto hashtable_list_sweep_again;
        }

        current = next;
        hashtable_hazard_ptr_set_with_mask(&hashtable->hazard, next, 1);
    }
}

/*
 * In the paper, deletion requires that on CAS failure the list should be
 * searched again for the appropriate node to delete. The problem with this
 * is that in doing so we won't be capable of holding onto a hazard pointer
 * to the result. By eliding the find in this case after CAS failure it will
 * take longer for the list to converge to an applicable state with no
 * deleted nodes. For the degenerated case, the likelyhood of having dead
 * nodes that are not deleted (because they were never traversed) is unlikely.
 *
 * Hazard pointer contract:
 *  - On entry hazard pointers shall be available
 *  - On exit (?, result, ?)
 *      - *result may be NULL
 */
static inline hash_mark_t hashtable_list_delete(
    hash_table_t *hashtable,
    size_t        bucket,
    hash_key_t    key,
    hash_size_t   code
) {
    hash_mark_t  result;
    hash_mark_t  next;
    hash_mark_t *prev;

    for (;;) {
        result = hashtable_list_find(hashtable, bucket, key, code, &prev);
        if (!result || result->code != code || result->key != key)
            return NULL;

        next = hashtable_hazard_ptr_get_current(&hashtable->hazard, 0);
        if (!hashtable_atomic_cas(
            &hashtable_node_get(result)->next,
            hashtable_node_make(hashtable_node_get(next), 0),
            hashtable_node_make(hashtable_node_get(next), 1)
        ))
            continue;

        if (hashtable_atomic_cas(
            prev,
            hashtable_node_make(hashtable_node_get(result), 0),
            hashtable_node_make(hashtable_node_get(next),   0)
        ))
            hashtable_node_destroy(hashtable_node_get(result));

        return result;
    }
}

/*
 * Insertion needs to perform a store barrier before it actually stores
 * something in the list to ensure all values for the given node are
 * globally visible.
 *
 * Hazard pointer contract:
 *  - On entry hazard pointers shall be available
 *  - On exit hazard pointers will contain
 *      - if (result == node) [next, current,  prev]
 *      - else                [node, currrent, prev]
 *          - *current may ne NULL
 */
static hash_mark_t hashtable_list_insert(hash_table_t *hashtable, size_t bucket, hash_node_t *node) {
    hash_mark_t  result;
    hash_mark_t *prev;
    hash_key_t   key  = node->key;
    hash_size_t  code = node->code;

    hashtable_atomic_sfence();

    for (;;) {
        result = hashtable_list_find(hashtable, bucket, key, code, &prev);
        if (result && result->code == node->code && result->key == node->key)
            return result;

        node->next = hashtable_node_make(hashtable_node_get(result), 0);
        hashtable_hazard_ptr_set(&hashtable->hazard, node, 0);

        if (hashtable_atomic_cas(
            prev,
            hashtable_node_make(hashtable_node_get(result), 0),
            hashtable_node_make(node,                       0)
        ))
            return node;
    }
}

static size_t hashtable_bucket_parent(size_t bucket) {
    for (size_t i = 31; i >= 0; --i)
        if (bucket & (1 << i))
            return bucket & ~(1 << i);
    return 0;
}

/*
 * Bucket initialization is protected by a non-recursive caller hazard
 * pointer.
 *
 * The table also needs to be reloaded on bucket initialization because
 * the previous hazard pointer will no longer be valid since it will be
 * holding the dummy node.
 */
static void hashtable_bucket_init(hash_table_t *hashtable, hash_mark_t *table, size_t bucket) {
    hash_mark_t result;
    size_t      parent = hashtable_bucket_parent(bucket);

    if (!hashtable_atomic_load(&table[parent]))
        hashtable_bucket_init(hashtable, table, parent);

    hash_node_t *node = HASHTABLE_CALLOC(sizeof(hash_node_t), 1);
    node->key  = (hash_key_t)(uintptr_t)bucket;
    node->code = hashtable_hash_key_dummy(bucket);

    result = hashtable_list_insert(hashtable, parent, node);
    if (hashtable_node_get(result) != node) {
        HASHTABLE_FREE(node);
        node = hashtable_node_get(result);
    }

    table = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&hashtable->table, 1);
    hashtable_atomic_store(&table[bucket], hashtable_node_make(node, 0));
}

static void hashtable_resize(hash_table_t *hashtable, size_t size) {
    hash_node_t **old = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&hashtable->table, 0);
    hash_node_t **new = HASHTABLE_CALLOC(sizeof(hash_node_t *), size << 1);

    memcpy(new, old, sizeof(hash_node_t *) * size);
    if (!hashtable_atomic_cas(&hashtable->size, size, size << 1)) {
        HASHTABLE_FREE(new);
        return;
    }

    if (!hashtable_atomic_cas((void **)&hashtable->table, old, new))
        HASHTABLE_FREE(new);
}

bool hashtable_insert(hash_table_t *hashtable, hash_key_t key, hash_value_t value) {
    hash_size_t  hash  = hashtable_hash_key(key);
    hash_node_t *node  = HASHTABLE_CALLOC(sizeof(hash_node_t), 1);
    hash_mark_t *table = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&hashtable->table, 0);

    node->code  = hashtable_hash_key_regular(hash);
    node->key   = key;
    node->value = value;

    size_t bucket = hash % hashtable->size;
    if (!table[bucket])
        hashtable_bucket_init(hashtable, table, bucket);

    if (hashtable_node_get(hashtable_list_insert(hashtable, bucket, node)) != node) {
        HASHTABLE_FREE(node);
        hashtable_hazard_ptr_clear_all(&hashtable->hazard);
        return false;
    }

    float size = (float)hashtable->size;
    if (hashtable_atomic_fai(&hashtable->count) / size > HASHTABLE_LOAD_FACTOR)
        hashtable_resize(hashtable, size);

    return true;
}

/* exposed interface */
hash_value_t hashtable_find(hash_table_t *hashtable, hash_key_t key) {
    hash_mark_t  result;
    hash_size_t  hash   = hashtable_hash_key(key);
    size_t       bucket = hash % hashtable->size;
    hash_mark_t *table  = hashtable_hazard_ptr_get(&hashtable->hazard, (void**)&hashtable->table, 0);
    hash_mark_t *prev;
    hash_node_t *node;

    if (!table[bucket])
        hashtable_bucket_init(hashtable, table, bucket);

    hash   = hashtable_hash_key_regular(hash);
    result = hashtable_list_find(hashtable, bucket, key, hash, &prev);
    node   = hashtable_node_get(result);

    if (node && node->code == hash && node->key == key) {
        hash_value_t value = NULL;
        if (hashtable->lvalue) {
            /* leave 0 for table */
            value = hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, &node->value, 0);
            hashtable_hazard_ptr_clear(&hashtable->hazard, 1);
            hashtable_hazard_ptr_clear(&hashtable->hazard, 2);
        } else {
            value = node->value;
            hashtable_hazard_ptr_clear_all(&hashtable->hazard);
        }
        return value;
    }
    return NULL;
}

/*
 * Resulting values will be set to NULL to ensure that the chance of others
 * getting a handle on the deleted node will be much smaller.
 */
static hash_value_t hashtable_delete(hash_table_t *hashtable, hash_key_t key) {
    hash_mark_t  result;
    hash_size_t  hash   = hashtable_hash_key(key);
    size_t       bucket = hash % hashtable->size;
    hash_mark_t *table  = hashtable_hazard_ptr_get(&hashtable->hazard, (void**)&hashtable->table, 0);
    hash_value_t value;

    if (!table[bucket])
        hashtable_bucket_init(hashtable, table, bucket);

    hash   = hashtable_hash_key_regular(hash);
    result = hashtable_list_delete(hashtable, bucket, key, hash);
    if (!result)
        return NULL;

    hashtable_atomic_fad(&hashtable->count);

    if (hashtable->lvalue) {
        value = hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, &hashtable_node_get(result)->value, 0);
        hashtable_hazard_ptr_clear(&hashtable->hazard, 1);
        hashtable_hazard_ptr_clear(&hashtable->hazard, 2);
    } else {
        value = hashtable_node_get(result)->value;
        hashtable_hazard_ptr_clear_all(&hashtable->hazard);
    }

    hashtable_atomic_store(&result->value, NULL);

    return value;
}

hash_table_t *hashtable_create(bool lvalue, size_t size) {
    hash_table_t *result   = HASHTABLE_CALLOC(sizeof(*result), 1);
    result->lvalue         = lvalue;
    result->size           = size;
    result->table          = HASHTABLE_CALLOC(sizeof(hash_node_t*), size);
    result->table[0]       = HASHTABLE_CALLOC(sizeof(hash_node_t), 1);
    result->table[0]->code = hashtable_hash_key_dummy(0);
    result->table[0]->key  = (hash_key_t)(uintptr_t)0;

    pthread_key_create(&result->hazard, NULL);
    return result;
}

void hashtable_destroy(hash_table_t *hashtable) {
    hash_mark_t *table   = hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&hashtable->table, 2);
    hash_node_t *current = hashtable_node_get(hashtable_hazard_ptr_get(&hashtable->hazard, (void **)&table[0], 0));
    hash_mark_t  next;

    for (; current; ) {
        hashtable_hazard_ptr_set_with_mask(&hashtable->hazard, current, 1);
        next = hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, (void **)&current->next, 0);
        if (!hashtable_node_get(next) && hashtable_node_regular(current->code)) {
            hash_value_t *value = (hashtable->lvalue)
                                    ? hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, &current->value, 2)
                                    : current->value;

            if (value) {
                while (!hashtable_atomic_cas(
                        &current->next,
                        hashtable_node_make(hashtable_node_get(next), 0),
                        hashtable_node_make(hashtable_node_get(next), 1)
                )) {
                    next = hashtable_hazard_ptr_get_with_mask(&hashtable->hazard, (void **)&current->next, 0);
                    if (hashtable_node_bit(next))
                        break;
                }
            }
        }
        HASHTABLE_FREE(current);
        current = hashtable_node_get(next);

    }
    HASHTABLE_FREE(hashtable->table);
    HASHTABLE_FREE(hashtable_hazard_ptr_table(&hashtable->hazard));
    pthread_key_delete(hashtable->hazard);
    HASHTABLE_FREE(hashtable);
}

#ifdef HASHTABLE_TEST
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef union {
    uintptr_t U;
    void     *P;
} entry_t;

#define P(V) (((entry_t){.U = (V)}).P)
#define U(V) (((entry_t){.P = (V)}).U)

#define ENTRIES 65536*2
#define STAGES  64
double populate(hash_table_t *ht, int current, int total) {
    srand(time(0));
    struct timespec b,e;
    printf("\r Running populater (%d/%d) ...", current, total);
    clock_gettime(CLOCK_REALTIME, &b);
    for (size_t i = 0; i < ENTRIES; i++)
        hashtable_insert(ht, P(rand()), P(rand()));
    clock_gettime(CLOCK_REALTIME, &e);
    return ((double)e.tv_sec - b.tv_sec) + ((double)e.tv_nsec - b.tv_nsec) / 1E9;
}

double fuzz(hash_table_t *ht, int current, int total) {
    srand(time(0));
    struct timespec b,e;
    printf("\r Running fuzzer (%d/%d) ...", current, total);
    clock_gettime(CLOCK_REALTIME, &b);

    volatile hash_value_t store;
    for (size_t i = 0; i < ENTRIES; i++) {
        if (i % 2)
            store = hashtable_find(ht, P(rand()));
        else
            store = hashtable_delete(ht, P(rand()));
        (void)store;
    }
    (void)store;
    clock_gettime(CLOCK_REALTIME, &e);
    return ((double)e.tv_sec - b.tv_sec) + ((double)e.tv_nsec - b.tv_nsec) / 1E9;
}

int main() {
    setbuf(stdout, 0);
    printf("This could take awhile (%zu entries)...\n", ENTRIES);
    hash_table_t *l[STAGES];
    hash_table_t *u[STAGES];
    double       lt[STAGES];
    double       ut[STAGES];
    double       ltt[STAGES];
    double       utt[STAGES];

    for (size_t i = 0; i < STAGES; i++) {
        l[i] = hashtable_create(true, 16);
        u[i] = hashtable_create(false, 16);

    }
    for (size_t i = 0,j = 1; i < STAGES; i++) {
        lt[i] = populate(l[i], j++, STAGES*2);
        ut[i] = populate(u[i], j++, STAGES*2);
    }
    printf("\n");
    for (size_t i = 0,j = 1; i < STAGES; i++) {
        ltt[i] = fuzz(l[i], j++, STAGES*2);
        utt[i] = fuzz(u[i], j++, STAGES*2);
    }
    printf("\n");

    double uu = 0;
    double ll = 0;
    double uuu = 0;
    double lll = 0;

    for (size_t i = 0; i < STAGES; i++) {
        printf("\r Running averager (%d/%d) ...", i+1, STAGES);
        uu  += ut [i];
        ll  += lt [i];
        uuu += utt[i];
        lll += ltt[i];
    }
    printf("\n");

    uu  /= STAGES;
    ll  /= STAGES;
    uuu /= STAGES;
    lll /= STAGES;

    FILE *fa = fopen("graph.dat", "w");
    FILE *fs = fopen("script.p", "w");
    if (!fa || !fs) {
        if (fa) fclose(fa);
        if (fs) fclose(fs);
        return EXIT_FAILURE;
    }

    fprintf(fa, "0 \"populate\" %lf\n", ll);
    fprintf(fa, "1 \"fuzz\"     %lf\n", lll);
    fprintf(fa, "2 \"populate\" %lf\n", uu);
    fprintf(fa, "3 \"fuzz\"     %lf\n", uuu);

    fclose(fa);

    fprintf(fs, "set title 'Locking-value/Non-locking-value hashtable manipulation w/%zu entries avg over %zu stages'\n", ENTRIES, STAGES);
    fprintf(fs, "set ylabel 'Time (avg seconds)'\n");
    fprintf(fs, "set xlabel 'Hashtable operations: populate (insert) fuzz (find and delete)'\n");
    fprintf(fs, "set style line 1 lc rgb \"red\"\n");
    fprintf(fs, "set style line 3 lc rgb \"blue\"\n");
    fprintf(fs, "set style fill solid\n");
    fprintf(fs, "set terminal png size 1024,768\n");
    fprintf(fs, "set output 'output.png'\n");
    fprintf(fs, "set boxwidth 0.5\n");
    fprintf(fs, "plot \"graph.dat\" every ::0::1 using 1:3:xtic(2) with boxes ls 1 title 'locking', \\\n");
    fprintf(fs, "     \"graph.dat\" every ::2::3 using 1:3:xtic(2) with boxes ls 2 title 'non-locking'\n");

    fclose(fs);

    system("gnuplot -e \"load 'script.p'\"");
    unlink("graph.dat");
    unlink("script.p");

    printf("Complete\n See output.png for comparision of value locking\n");
    return EXIT_SUCCESS;
}
#endif
