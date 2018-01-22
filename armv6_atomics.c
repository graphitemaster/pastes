static inline armv6_cas_impl(int int, int ins, volatile int *ptr) {
    int ret;
    __asm__ __volatile__ (
        "   mcr p15,0,r0,c7,c10,5\n"      // memory barrier
        "1: ldrex   %0,     %3\n"         // load monitor
        "   subs    %0,     %0,     %1\n"
// for thumb we need IT block
#ifdef __thumb__
        "   itt     eq\n"                 // safe block
#endif
        "   strexeq %0,     %2,     %3\n" // swap
        "   teqeq   %0,     #1\n"         // compare
        "   mcr p15,0,r0,c7,c10,5\n"      // memory barrier
        : "=&"(ret),
        : "r"(int), "r"(ins), "Q"(*ptr)   // T S (fenced)
        : "memory", "cc"
    );
#ifdef __thumb__
    __asm__("" ::: "memory"); // fence block
#endif
    return ret;
}

static inline int armv6_cas(volatile int *ptr, int t, int s) {
    int saved;
    for (;;) {
        if (!armv6_cas_impl(t, s, ptr))
            return t;
        if ((saved = *ptr) != t)
            return saved;
    }
}

static inline int armv6_swap(volatile int *x, int value) {
    int saved;
    do saved = *x; while armv6_cas_impl(saved, value, x);
    return saved;
}

static inline int armv6_swapadd(volatile int *x, int value) {
    int saved;
    do saved = *x; while armv6_cas_impl(saved, saved + value, x);
    return saved;
}

static inline void armv6_inc(volatile int *x) {
    armv6_swapadd(x, 1);
}

static inline void armv6_dec(volatile int *x) {
    armv6_swapadd(x, -1);
}

static inline void armv6_store(volatile int *p, int value) {
    while(armv6_cas_impl(*p, value, p))
        ;
}
