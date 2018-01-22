#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
} buffer_t;

static bool buffer_init(buffer_t *self) {
    self->capacity = 1 << 20; // 1MB
    self->length = 0;
    if (!(self->data = malloc(self->capacity)))
        return false;
    *self->data = '\0';
    return true;
}

static void buffer_destroy(buffer_t *self) {
    free(self->data);
}

static bool buffer_append(buffer_t *self, const char *data, size_t length) {
    if (self->length + length >= self->capacity) {
        self->capacity *= 2;
        char *old = self->data;
        if (!(self->data = realloc(self->data, self->capacity))) {
            self->data = old;
            buffer_destroy(self);
            return false;
        }
    }
    memcpy(self->data + self->length, data, length);
    self->length += length;
    self->data[self->length] = '\0';
    return true;
}

typedef struct regex_entry_s {
    char *search;
    char *replace;
    struct regex_entry_s *next;
} regex_entry_t;

static bool regex_entry_init(regex_entry_t *entry, char *search, size_t searchlen, char *replace, size_t replacelen) {
    if (!(entry->search = strndup(search, searchlen)))
        return false;
    if (!(entry->replace = strndup(replace, replacelen))) {
        free(entry->search);
        return false;
    }
    entry->next = NULL;
    return true;
}

static void regex_entry_destroy(regex_entry_t *entry) {
    free(entry->search);
    free(entry->replace);
}

static bool read(buffer_t *self, FILE *fp) {
    char buffer[1024];
    while (!feof(fp)) {
        const size_t count = fread(buffer, 1, sizeof(buffer), fp);
        if (!buffer_append(self, buffer, count))
            return false;
    }
    return true;
}

static bool write(buffer_t *self, char **file) {
    *file = tempnam("/tmp", "cregp");
    if (!*file) return false;
    FILE *fp = fopen(*file, "w");
    if (!fp) return false;
    fprintf(fp, "%s", self->data);
    fclose(fp);
    return true;
}

static void oom() {
    fprintf(stderr, "out of memory\n");
    abort();
}

static bool process(buffer_t *buffer, buffer_t *out, regex_entry_t *head) {
    char *data = buffer->data;
    regex_entry_t **next = &head->next;
    while (*data) {
        if (!strncmp(data, "//", 2)) {
            // Skip single-line comments
            while (!strchr("\r\n", *data)) *data++;
        } else if (!strncmp(data, "/*", 2)) {
            // Skip multi-line comments
            while (strncmp(data, "*/", 2))
                *data++;
        } else if (*data == '\'') {
            // Skip single-quoted string
            if (!buffer_append(out, data, 1)) return false;
            data++;
            while (*data && *data != '\'') {
                if (!buffer_append(out, data, 1)) return false;
                data++;
            }
        } else if (*data == '\"') {
            // Skip double-quote string
            if (!buffer_append(out, data, 1)) return false;
            data++;
            while (*data && *data != '\"') {
                if (!buffer_append(out, data, 1)) return false;
                data++;
            }
        } else if (!strncmp(data, "#define", 7)) {
            char *macro = data + 7;
            while (isspace(*macro)) macro++;
            if (!strncmp(macro, "s/", 2)) {
                char *search_beg = macro + 2;
                char *search_end = strchr(search_beg, '/');
                char *replace_beg = search_end + 1;
                char *replace_end = strchr(replace_beg, '/');
                if (!replace_beg || !replace_end) abort();
                regex_entry_t *e = malloc(sizeof *e);
                if (!e) return false;
                if (!regex_entry_init(e, search_beg, search_end - search_beg,
                                         replace_beg, replace_end - replace_beg))
                    return false;
                *next = e;
                next = &e->next;
                data = replace_end + 1;
            }
        }
        if (!buffer_append(out, data, 1)) return false;
        data++;
    }
    return true;
}

int main() {
    FILE *fp = stdin;
    buffer_t i, o;
    if (!buffer_init(&i)) oom();
    if (!buffer_init(&o)) oom();
    if (!read(&i, fp)) oom();

    regex_entry_t list;
    memset(&list, 0, sizeof(list));

    if (!process(&i, &o, &list)) oom();

    char *file = NULL;
    if (!write(&o, &file)) oom();

    for (regex_entry_t *e = list.next; e; ) {
        regex_entry_t *n = e->next;
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "perl -pi -e \"s/%s/%s/g\" %s", e->search, e->replace, file);
        system(buffer);
        regex_entry_destroy(e);
        free(e);
        e = n;
    }
    buffer_destroy(&i);
    buffer_destroy(&o);

    fp = fopen(file, "r");
    if (!fp) abort();
    if (!buffer_init(&i)) oom();
    if (!read(&i, fp)) oom();

    printf("%s", i.data);

    buffer_destroy(&i);
    free(file);
}
