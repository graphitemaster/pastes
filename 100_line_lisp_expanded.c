#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct { char *name; } atom_t;
typedef struct { int type; void *sexp; } sexp_t, *o;
typedef struct { sexp_t *car, *cdr; } cons_t;
typedef struct { o (*f)(o[6]); char min, max; const char *name; } subr_t;
static sexp_t *env = 0, *g_nil, *g_t, *g_quote, *g_unbound, *g_error;
void __attribute__((constructor)) init() {
    g_nil = memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(atom_t)), (&((atom_t){strdup("nil")})), sizeof(atom_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), g_t = memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(atom_t)), (&((atom_t){strdup("t")})), sizeof(atom_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), g_quote = memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(atom_t)), (&((atom_t){strdup("quote")})), sizeof(atom_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))),
    g_unbound = memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(atom_t)), (&((atom_t){strdup("unbound")})), sizeof(atom_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), g_error = memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(atom_t)), (&((atom_t){strdup("error")})), sizeof(atom_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))); }
char *str_cons(cons_t *c, int r);
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
static int r = 0;
sexp_t *read_from(char *s, int b);
sexp_t *read_atom(char *s, int b) {
    int l = 0; char *n = 0;
    while (((*__ctype_b_loc ())[(int) ((s[b]))] & (unsigned short int) _ISspace)) b++;
    while (s[b] && !((*__ctype_b_loc ())[(int) ((s[b]))] & (unsigned short int) _ISspace) && !strchr("()", s[b])) b++, l++; r = b;
    return memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(atom_t)), (&((atom_t){strdup(((b-l) >= (b) ? 0 : strncpy(calloc(1, (b)-(b-l)+1), (s)+(b-l), (b)-(b-l))))})), sizeof(atom_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){0})), sizeof(sexp_t)))); }
sexp_t *read_cons(char *s, int b) {
    while (((*__ctype_b_loc ())[(int) ((s[b]))] & (unsigned short int) _ISspace)) b++;
    if (s[b] == ')') { r = ++b; return calloc(1, sizeof(sexp_t)); }
    if (s[b] == '.') return read_atom(s, b+1);
    return memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(cons_t)), (&((cons_t){read_from(s, b), read_cons(s, r)})), sizeof(cons_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))); }
sexp_t *read_from(char *s, int b) {
    while (((*__ctype_b_loc ())[(int) ((s[b]))] & (unsigned short int) _ISspace)) b++;
    if (s[b] == '(') { r++; return read_cons(s, b+1); }
    else if (s[b] == '\'') { r++; return memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(cons_t)), (&((cons_t){g_quote, memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(cons_t)), (&((cons_t){read_from(s, b+1), calloc(1, sizeof(sexp_t))})), sizeof(cons_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t))))})), sizeof(cons_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))); }
    else if (s[b] == ')') { r++; return g_nil; }
    return read_atom(s, b); }
sexp_t *read_sexp(char *s) { return read_from(s, 0); }
o Fcons(o a[6]); static subr_t Scons = { .f = Fcons, .min = (2), .max = (2) }; o Fcons(o a[6]) { return memcpy(calloc(1, sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))), (memcpy(calloc(1, sizeof(cons_t)), (&((cons_t){a[0], a[1]})), sizeof(cons_t))), sizeof(memcpy(calloc(1, sizeof(sexp_t)), (&((sexp_t){1})), sizeof(sexp_t)))); }
o Fcar(o a[6]); static subr_t Scar = { .f = Fcar, .min = (1), .max = (1) }; o Fcar(o a[6]) { return a[0] && a[0]->type ? ((cons_t*)(a[0]->sexp))->car : g_nil; }
o Fcdr(o a[6]); static subr_t Scdr = { .f = Fcdr, .min = (1), .max = (1) }; o Fcdr(o a[6]) { return a[0] && a[0]->type ? ((cons_t*)(a[0]->sexp))->cdr : g_nil; }
o Fquote(o a[6]); static subr_t Squote = { .f = Fquote, .min = (1), .max = (1) }; o Fquote(o a[6]) { return a[0]; }
o Fif_(o a[6]); static subr_t Sif_ = { .f = Fif_, .min = (2), .max = (3) }; o Fif_(o a[6]) { return !((a[0]) && !(a[0])->type && !strcmp(str_sexp((a[0])), "nil")) ? a[1] : a[2]; }
o Feq(o a[6]); static subr_t Seq = { .f = Feq, .min = (2), .max = (2) }; o Feq(o a[6]) { return !strcmp(str_sexp(a[0]), str_sexp(a[1])) ? g_t : g_nil; }
o Fatom(o a[6]); static subr_t Satom = { .f = Fatom, .min = (1), .max = (1) }; o Fatom(o a[6]) { return a[0] ? (a[0]->type ? Feq((o[]){a[0], g_t}) : g_t) : g_nil; }
o Fdef(o a[6]); static subr_t Sdef = { .f = Fdef, .min = (2), .max = (2) }; o Fdef(o a[6]) { env = Fcons((o[]){a[0], a[1], env}); return a[0]; }
sexp_t *nth(int n, o sexp) {
    while (n > 0 && !((sexp) && !(sexp)->type && !strcmp(str_sexp((sexp)), "nil"))) sexp = Fcdr((o[]){sexp}), n--;
    return Fcdr((o[]){sexp}); }
sexp_t *assoc(sexp_t *k, sexp_t *p) {
    sexp_t *car = Fcar((o[]){p}), *cdr = Fcdr((o[]){p});
    if (((k) && !(k)->type && !strcmp(str_sexp((k)), "nil"))) return g_nil;
    if (((p) && !(p)->type && !strcmp(str_sexp((p)), "nil"))) return g_unbound;
    if (!((Feq((o[]){Fcar((o[]){car}), k})) && !(Feq((o[]){Fcar((o[]){car}), k}))->type && !strcmp(str_sexp((Feq((o[]){Fcar((o[]){car}), k}))), "nil"))) return Fcdr((o[]){car});
    return assoc(k, Fcdr((o[]){p})); }
sexp_t *eval(sexp_t *sexp, sexp_t *localenv);
sexp_t *expand_or_call(sexp_t *macro, sexp_t *args, int call) {
    sexp_t *localenv = memcpy(calloc(1, sizeof(sexp_t)), (env), sizeof(sexp_t)), *al = nth(1, macro), *vl = args,
    *bl = Fcar((o[]){Fcdr((o[]){Fcdr((o[]){macro})})}), *car1, *cdr1, *car2, *cdr2;
    do { car1 = Fcar((o[]){al}), cdr1 = Fcdr((o[]){al}),
         car2 = Fcar((o[]){vl}), cdr2 = Fcdr((o[]){vl});
         localenv = Fcons((o[]){Fcons((o[]){car1, call ? eval(car2, env) : car2}), localenv});
    } while (!((cdr1) && !(cdr1)->type && !strcmp(str_sexp((cdr1)), "nil")) && ((cdr2) && !(cdr2)->type && !strcmp(str_sexp((cdr2)), "nil")));
    return eval(bl, localenv); }
sexp_t *eval(sexp_t *sexp, sexp_t *localenv) {
    if (!sexp) return g_nil;
    if (sexp->type == 0) return assoc(sexp, localenv);
    sexp_t *car = Fcar((o[]){sexp}), *cdr = Fcdr((o[]){sexp});
    if (!((Fatom((o[]){car})) && !(Fatom((o[]){car}))->type && !strcmp(str_sexp((Fatom((o[]){car}))), "nil"))) {
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
        if (((car) && (car)->type && !strcmp(str_sexp(Fcar((o[]){(car)})), "lambda"))) return expand_or_call(car, cdr, 1);
        else if (((car) && (car)->type && !strcmp(str_sexp(Fcar((o[]){(car)})), "macro"))) return expand_or_call(car, cdr, 0);
    }
    return g_error;}
