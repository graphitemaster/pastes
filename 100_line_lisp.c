#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct { char *name; } atom_t; // atom
typedef struct { int type; void *sexp; } sexp_t, *o; // s-expression & object
typedef struct { sexp_t *car, *cdr; } cons_t; // cons
typedef struct { o (*f)(o[6]); char min, max; const char *name; } subr_t; // subroutine
static sexp_t *env = 0, *g_nil, *g_t, *g_quote, *g_unbound, *g_error; // globals
#define SUBSTR(S, B, E) ((B) >= (E) ? 0 : strncpy(calloc(1, (E)-(B)+1), (S)+(B), (E)-(B))) // make & copy atoms/cons/sexpr
#define EMPTY(TYPE) calloc(1, sizeof(TYPE))
#define COPY(TYPE, WHAT) memcpy(EMPTY(TYPE), (WHAT), sizeof(TYPE))
#define MAKE(TYPE, ...) COPY(TYPE, &((TYPE){__VA_ARGS__}))
#define MSEXP(TYPE, ...) COPY(MAKE(sexp_t, TYPE), __VA_ARGS__)
#define MATOM(STR) MSEXP(0, MAKE(atom_t, strdup(STR)))
#define MCONS(CAR, CDR) MSEXP(1, MAKE(cons_t, CAR, CDR))
void __attribute__((constructor)) init() {
    g_nil = MATOM("nil"), g_t = MATOM("t"), g_quote = MATOM("quote"),
    g_unbound = MATOM("unbound"), g_error = MATOM("error"); }
char *str_cons(cons_t *c, int r); // atom/cons/sexpr to string
char *str_atom(atom_t *a) { return a ? a->name : "nil"; }
char *str_sexp(sexp_t *s) { return s ? (s->type ? str_cons(s->sexp, 1) : str_atom(s->sexp)) : "nil"; }
char *str_cons(cons_t *c, int r) {
    if (!c) return "nil";
    char *carstr = str_sexp(c->car);
    if (!c->cdr) return carstr;
    char *cdrstr = c->cdr->type ? str_cons(c->cdr->sexp, 0) : str_atom(c->cdr->sexp);
    size_t len = strlen(carstr) + strlen(cdrstr) + 6;
    char *newstr = calloc(1, len);
    if (r) strcat(newstr, "(");
    strcat(newstr, carstr);
    if (strcmp(cdrstr, "nil")) strcat(newstr, c->cdr->type ? " " : " . "), strcat(newstr, cdrstr);
    if (r) strcat(newstr, ")");
    return newstr; }
static int r = 0; // parser/lexer
sexp_t *read_from(char *s, int b);
sexp_t *read_atom(char *s, int b) {
    int l = 0; char *n = 0;
    while (isspace(s[b])) b++;
    while (s[b] && !isspace(s[b]) && !strchr("()", s[b])) b++, l++; r = b;
    return MATOM(SUBSTR(s, b-l, b)); }
sexp_t *read_cons(char *s, int b) {
    while (isspace(s[b])) b++;
    if (s[b] == ')') { r = ++b; return EMPTY(sexp_t); }
    if (s[b] == '.') return read_atom(s, b+1);
    return MCONS(read_from(s, b), read_cons(s, r)); }
sexp_t *read_from(char *s, int b) {
    while (isspace(s[b])) b++;
    if (s[b] == '(') { r++; return read_cons(s, b+1); }
    else if (s[b] == '\'') { r++; return MCONS(g_quote, MCONS(read_from(s, b+1), EMPTY(sexp_t))); }
    else if (s[b] == ')') { r++; return g_nil; }
    return read_atom(s, b); }
sexp_t *read_sexp(char *s) { return read_from(s, 0); }
#define FUN(NAME, MIN, MAX) \
    o F##NAME(o a[6]); static subr_t S##NAME = { .f = F##NAME, .min = (MIN), .max = (MAX) }; \
    o F##NAME(o a[6]) // core runtime (for predicates)
FUN(cons, 2, 2) { return MCONS(a[0], a[1]); }
FUN(car, 1, 1) { return a[0] && a[0]->type ? ((cons_t*)(a[0]->sexp))->car : g_nil; }
FUN(cdr, 1, 1) { return a[0] && a[0]->type ? ((cons_t*)(a[0]->sexp))->cdr : g_nil; }
FUN(quote, 1, 1) { return a[0]; }
#define NILP(SEXP) ((SEXP) && !(SEXP)->type && !strcmp(str_sexp((SEXP)), "nil")) // predicates
#define LAMBAP(SEXP) ((SEXP) && (SEXP)->type && !strcmp(str_sexp(Fcar((o[]){(SEXP)})), "lambda"))
#define MACROP(SEXP) ((SEXP) && (SEXP)->type && !strcmp(str_sexp(Fcar((o[]){(SEXP)})), "macro"))
FUN(if_, 2, 3) { return !NILP(a[0]) ? a[1] : a[2]; } // rest of the runtime (using predicates)
FUN(eq, 2, 2) { return !strcmp(str_sexp(a[0]), str_sexp(a[1])) ? g_t : g_nil; }
FUN(atom, 1, 1) { return a[0] ? (a[0]->type ? Feq((o[]){a[0], g_t}) : g_t) : g_nil; }
FUN(def, 2, 2) { env = Fcons((o[]){a[0], a[1], env}); return a[0]; }
sexp_t *nth(int n, o sexp) {
    while (n > 0 && !NILP(sexp)) sexp = Fcdr((o[]){sexp}), n--;
    return Fcdr((o[]){sexp}); }
sexp_t *assoc(sexp_t *k, sexp_t *p) {
    sexp_t *car = Fcar((o[]){p}), *cdr = Fcdr((o[]){p});
    if (NILP(k)) return g_nil;
    if (NILP(p)) return g_unbound;
    if (!NILP(Feq((o[]){Fcar((o[]){car}), k}))) return Fcdr((o[]){car});
    return assoc(k, Fcdr((o[]){p})); }
sexp_t *eval(sexp_t *sexp, sexp_t *localenv);
sexp_t *expand_or_call(sexp_t *macro, sexp_t *args, int call) {
    sexp_t *localenv = COPY(sexp_t, env), *al = nth(1, macro), *vl = args,
    *bl = Fcar((o[]){Fcdr((o[]){Fcdr((o[]){macro})})}), *car1, *cdr1, *car2, *cdr2;
    do { car1 = Fcar((o[]){al}), cdr1 = Fcdr((o[]){al}),
         car2 = Fcar((o[]){vl}), cdr2 = Fcdr((o[]){vl});
         localenv = Fcons((o[]){Fcons((o[]){car1, call ? eval(car2, env) : car2}), localenv});
    } while (!NILP(cdr1) && NILP(cdr2));
    return eval(bl, localenv); } // macro expansion is magic
sexp_t *eval(sexp_t *sexp, sexp_t *localenv) { // evaluator
    if (!sexp) return g_nil;
    if (sexp->type == 0) return assoc(sexp, localenv);
    sexp_t *car = Fcar((o[]){sexp}), *cdr = Fcdr((o[]){sexp});
    if (!NILP(Fatom((o[]){car}))) {
        char *str = str_sexp(car);
        if (!strcmp(str, "quote")) return Fcar((o[]){cdr});
        else if (!strcmp(str, "atom")) return Fatom((o[]){eval(nth(1, sexp), localenv)});
        else if (!strcmp(str, "eq")) return Feq((o[]){eval(nth(1, sexp), localenv),eval(nth(2, sexp), localenv)});
        else if (!strcmp(str, "car")) return Fcar((o[]){eval(nth(1, sexp), localenv)});
        else if (!strcmp(str, "cdr")) return Fcdr((o[]){eval(nth(1, sexp), localenv)});
        else if (!strcmp(str, "cons")) return Fcons((o[]){eval(nth(1, sexp), localenv),eval(nth(2, sexp), localenv)});
        else if (!strcmp(str, "if")) return Fif_((o[]){eval(nth(1, sexp), localenv),eval(nth(2, sexp), localenv),eval(nth(3, sexp), localenv)});
        else if (!strcmp(str, "def")) return Fdef((o[]){nth(1, sexp), eval(nth(2, sexp), localenv)});
        else if (!strcmp(str, "lambda") || !strcmp(str, "macro")) return sexp;
        else if (!strcmp(str, "unbound")) return g_unbound;
        else if (!strcmp(str, "eval")) return eval(nth(1, sexp), localenv);
        else { sexp = eval(car, localenv); return eval(Fcons((o[]){car, cdr}), localenv); }
    } else {
        if (LAMBAP(car)) return expand_or_call(car, cdr, 1);
        else if (MACROP(car)) return expand_or_call(car, cdr, 0);
    }
    return g_error; }
