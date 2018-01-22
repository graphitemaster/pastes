#include <string.h>
#include <stdio.h>
static int a(int c) {
    return c == ' ' || c >= '\t' && c <= '\r';
}
static char *b(char *s) {
    for (char *p = s + strlen(s); p > s && a(*--p); *p = '\0'); return s;
}
static char *d(char *s) {
    for (; *s && a(*s); s++); return s;
}
static char *g(const char *s, char ch) {
    for (int w = 0; *s && *s != ch && !(w && *s == ';'); w = a(*s), s++); return s;
}
static void h(char *d, const char *o, size_t s) {
    strncpy(d, o, s), d[s - 1] = '\0';
}
size_t parse(FILE *f, int (*c)(void *, const char *, const char *, const char *), void *u) {
    char l[512], t[512] = "", p[512] = "", *s, *e, *n, *v; size_t r = 0;
    for (size_t o = 0; fgets(l, sizeof l, f); o++) {
        if (strchr(";#", *(s = d(b(l))))) ; else if (*s == '[') {
            if (*(e = g(s + 1, ']'))) *e = 0, h(t, s + 1, sizeof t), *p = 0; else if (r == 0) r = o;
        } else if (*s && *s != ';') {
            if (*(e = g(s, '=')) != '=') e = g(s, ':');
            if (strchr("=:", *e)) {
                *e = 0, n = b(s), v = d(e + 1);
                if (*(e = g(v, 0)) == ';') *e = 0;
                b(v), h(p, n, sizeof p);
                if (c(u, t, n, v) != 0 && r == 0) r = o;
            } else if (r == 0) r = o;
        }
    }
    return r;
}
