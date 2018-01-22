/*
 * A mark and sweep GC for C. Placed in the public domain.
 * written by Dale Weiler.
 *
 * to compile gcc -std=gnu99 file.c -o test
 * to run test ./test.
 *
 * see line 224 for indepth explination and example.
 */

/* begin header */
#ifndef GC_HDR
#define GC_HDR
#include <stddef.h>

#define GC

typedef unsigned char gc_byte_t;

typedef struct gc_s gc_t;
gc_t *gc_create(size_t offset);
void gc_destroy(gc_t *gc);

void gc_free(gc_t *gc, void *pointer);
void gc_root(gc_t *gc, void *root);
size_t gc_collect(gc_t *gc);
void *gc_alloc(gc_t *gc, size_t size, gc_byte_t children);
void *gc_realloc(gc_t *gc, void *ptr, size_t size);
char *gc_strdup(gc_t *gc, const char *string);

#endif
/* end header */

/* begin source */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#define GC_ROOTS 32
#define GC_MAGIC 0x47

typedef struct {
    gc_byte_t magic;
    gc_byte_t referenced;
    gc_byte_t children;
    gc_byte_t index;
    void     *next;
    void     *prev;
} gc_object_t;

typedef struct {
    void *beg;
    void *end;
} gc_heap_t;

typedef struct {
    void **data[GC_ROOTS];
    size_t size;
} gc_root_t;

struct gc_s {
    gc_object_t *head;
    gc_root_t    root;
    gc_heap_t    heap;
    size_t       count;
    size_t       offset;
};

void gc_free(gc_t *gc, void *pointer) {
    gc_object_t *object = (gc_object_t*)pointer - 1;
    if (object->prev)
        ((gc_object_t*)object->prev)->next = object->next;
    else
        gc->head = object->next;
    if (object->next)
        ((gc_object_t*)object->next)->prev = object->prev;
    object->magic = 0;
    gc->count--;
    free(object);
}

void gc_root(gc_t *gc, void *root) {
    assert(gc->root.size < sizeof(gc->root.data) / sizeof(void *));
    gc->root.data[gc->root.size++] = (void**)root;
}

/* The mark stage */
static void gc_mark(gc_t *gc, gc_object_t *object) {
    if (object->referenced)
        return;

    object->referenced = 1;
    object->prev       = NULL;

    while (object) {
        /* There is additional offset */
        unsigned char *top = (unsigned char *)(object + 1) + gc->offset;
        /* Go from top of stack */
        gc_object_t **references = (gc_object_t**)top;
        for (size_t i = object->index; i < object->children; ++i) {
            ++object->index;
            if (!references[i])
                continue;

            /* Verify pointer */
            gc_object_t *obj = references[i] - 1;
            if ((uintptr_t)gc->heap.beg > (uintptr_t)obj)
                continue;
            if ((uintptr_t)obj > (uintptr_t)gc->heap.end)
                continue;
            if (((uintptr_t)obj & 3) != 0)
                continue;
            if (obj->magic != GC_MAGIC)
                continue;
            if (obj->referenced != 0)
                continue;

            obj->prev          = object;
            object             = obj;
            object->referenced = 1;
            break;
        }
        /* Do backwards walk */
        if (object->index >= object->children)
            object = object->prev;
    }
}

/* The sweep phase */
size_t gc_collect(gc_t *gc) {
    size_t collected = 0;

    /* Mark referenced objects */
    for (size_t i = 0; i < sizeof(gc->root.data)/sizeof(void*); ++i) {
        if (gc->root.data[i] && *gc->root.data[i])
            gc_mark(gc, (gc_object_t*) *gc->root.data[i] - 1);
    }

    /* Sweep */
    gc_object_t *object = gc->head;
    gc_object_t *next   = NULL;
    gc_object_t *prev   = NULL;
    for (; object; object = next) {
        next = object->next;
        if (object->referenced == 0) {
            gc_free(gc, object + 1);
            ++collected;
            continue;
        }
        object->prev       = prev;
        object->referenced = 0;
        object->index      = 0;
        prev               = object;
    }
    return collected;
}

void *gc_alloc(gc_t *gc, size_t size, gc_byte_t children) {
    gc_object_t *object;
    if (!(object = malloc(sizeof(*object) + size)))
        return NULL;

    if ((uintptr_t)object < (uintptr_t)gc->heap.beg)
        gc->heap.beg = object;
    if ((uintptr_t)object > (uintptr_t)gc->heap.end)
        gc->heap.end = object;

    memset(object, 0, sizeof(*object) + size);
    object->magic      = GC_MAGIC;
    object->referenced = 0;
    object->children   = (gc_byte_t)children;
    object->index      = 0;
    object->next       = gc->head;
    object->prev       = NULL;

    if (gc->head)
        gc->head->prev = object;
    gc->head = object;

    gc->count++;
    return object + 1;
}

void *gc_realloc(gc_t *gc, void *ptr, size_t size) {
    size += sizeof(gc_object_t);
    void *moved = realloc(ptr, size);
    gc_object_t *object = moved;

    /* If it moved */
    if ((uintptr_t)object < (uintptr_t)gc->heap.beg)
        gc->heap.beg = object;
    if ((uintptr_t)object > (uintptr_t)gc->heap.end)
        gc->heap.end = object;

    return object + 1;
}

char *gc_strdup(gc_t *gc, const char *string) {
    size_t length = strlen(string) + 1;
    char *data = gc_alloc(gc, length, 0);
    memcpy(data, string, length);
    return data;
}

gc_t *gc_create(size_t offset) {
    gc_t *gc = calloc(1, sizeof(gc_t));
    if (!gc)
        return NULL;

    gc->offset = offset;
    return gc;
}

void gc_destroy(gc_t *gc) {
    memset(gc->root.data, 0, sizeof(gc->root.data) / sizeof(void*));
    gc_collect(gc);
    free(gc);
}

/* end source */

/* begin example */

typedef struct {
    void GC *left;
    void GC *right;
    int      value;
} node_t;

static node_t *top;
static node_t *nodes[4];

int main(void) {
    /*
     * 0 can be sub'd for an 'offsetof' first pointer in structure.
     * in our case it's 0, as the pointers begin right at the top. In
     * more limited cases structures may already have headers which
     * cannot move (boxed values) so the 'offsetof' argument allows you
     * to specify the true offset to pointers for the GC.
     */
    gc_t *gc = gc_create(0);
    for (size_t i = 0; i < sizeof(nodes)/sizeof(*nodes); i++) {
        /*
         * We specify 2 for the children because node_t contains two
         * children leafs, left and right. This value can be subsituted
         * for something else like 1 (if dealing with single-linked list)
         * or N (if dealing with an array of N elements).
         */
        nodes[i] = gc_alloc(gc, sizeof(node_t), 2);
    }

    /*
     * The GC understands the relationship of nodes but it doesn't know
     * where to begin it's marking, typically a GC has 'roots', globals
     * stack, thread-local, function-local are examples of this. Lets
     * make a dummy root.
     */
    gc_root(gc, (void*)&top);


    /*
     * Now lets construct a hiearchy of references and alike with the
     * pointers we've obtained. Yes, we can do cyclic too.
     *
     * The tree should look something like this:
     *
     * | top -> node[0]
     * |        /    \
     * |     node[2]  NULL
     * |     /    \
     * |  node[0]  node[3]
     *
     */
    nodes[0]->left  = nodes[2];
    nodes[2]->left  = nodes[0];
    nodes[2]->right = nodes[3];
    nodes[3]->value = 1234;
    top             = nodes[0];

    /* nodes[1] isn't referenced, the following should free it */
    size_t count = 0;
    count = gc_collect(gc); printf("collected %zu object(s)\n", count);

    /*
     * Now we unmark the right side of nodes[2] which effectively implies
     * that nodes[3] isn't referenced anymore.
     */
    nodes[2]->right = NULL;

    /* nodes[3] isn't referenced, the following should free it */
    count = gc_collect(gc); printf("collected %zu object(s)\n", count);

    /*
     * Up to this point top has exclusive ownership over node[0] which
     * means nodes[0] and nodes[2] are still referenced. Lets eliminate
     * the rest of the referenced by killing the root.
     */
    top = NULL;

    /* nodes[0] and nodes[2] are not referenced, the following should free both */
    count = gc_collect(gc); printf("collected %zu object(s)\n", count);

    /* no leaks are possible */
}

/* end example */
