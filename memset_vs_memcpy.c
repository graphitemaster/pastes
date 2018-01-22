// cc benchmark.c -std=gnu99 -O3 -lgmp -o benchmark
//
// Note: System must have gnuplot installed for graphs
//
// #define USE_RDTSC to use CPU TSC (unreliable)
// #define USE_GNUPLOT to enable graph generation

#define USE_GNUPLOT

#define MB(x) ((x) << 20)
#define KB(x) ((x) << 10)

// The size of the block of memory to zero out
#define TEST_SIZE MB(64)
// The amount of times to zero it out
#define TEST_ITERS 1024

// How many memory pools of `TEST_SIZE' to make to trample CPU cache.
//
// Note: TEST_SIZE * TEST_STRIDE * 2 bytes of physical memory must exist on
// the system (all of it is comitted.)
#define TEST_STRIDE 32

// The value of m and n for ackermanns function which is used before running
// the tests to get the CPU spun up and working.
//
// Note: Anything higher than 5 will require a lot of stack `ulimit -s unlimited'
#define CPU_SPINUP_FACTOR 3

// Explination:
// TEST_STRIDE memory pools are created of size TEST_SIZE.
//
// TEST_STRIDE memory pools are created of size TEST_SIZE which are populated with
// random data from /dev/urandom.
//
// On each iteration a memory pool is allocated (next pool) to be used in the
// zeroing phase. If we run out of pools (when TEST_STRIDE is reached) all the
// pools are overwritten with random data from the next random pool, then the
// first pool is returned and that process repeats. There is always a next random
// pool, when TEST_STRIDE is reached the first pool is returned just like the
// memory pool, it's cylic.
//
// Tests themselves are not performed without spinning up the CPU first

// C standard includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// Posix includes
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h> // clock_gettime
#include <unistd.h>

// Third party includes
#include <gmp.h>

#define INLINE inline
#define NOINLINE __attribute__((noinline))

typedef unsigned long long ull_t;

#ifdef USE_RDTSC
#   ifndef __x86_64__
#   define TIME_STAMP_NAME "cpu ticks"
    static INLINE ull_t tsc(void) {
        unsigned int hi;
        unsigned int lo;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return ((ull_t)lo) | (((ull_t)hi) << 32);
    }
#   else
    static INLINE ull_t tsc(void) {
        unsigned long long int x;
        __asm__ __volatile__("rdtsc" : "=A"(x));
        return x;
    }
#   endif
#else
#   define TIME_STAMP_NAME "nano seconds"
    static inline ull_t tsc(void) {
        struct timespec s;
        clock_gettime(CLOCK_REALTIME, &s);
        return s.tv_nsec;
    }
#endif

/// An allocator which puts an emphasis on forcing commit, preventign zero-page
/// optimizations and most importantly ensuring that each test iteration operates
/// On a new piece of memory. Since we can't allocate memory for every single
/// iteration we use a stride instead which should prevent caching. When the
/// Stride is hit we erase the pool with random data.
static unsigned char **pool_data = NULL;
static unsigned char **pool_random = NULL;
static size_t pool_random_index = 0;
static size_t pool_data_index = 0;

static void __attribute__((constructor)) setup_pool_data(void) {
    // Calculate total amount of memory this benchmark will consume
    mpz_t total;
    mpz_init(total);
    mpz_set_ui(total, TEST_SIZE);
    mpz_mul_ui(total, total, TEST_STRIDE);
    mpz_mul_ui(total, total, 2);

    printf("The following benchmark will need at least ");
    mpz_out_str(stdout, 10, total);
    printf(" bytes to run\n\n");
    mpz_clear(total);

    printf("Allocating %zu memory pools of size %zu (bytes)\n",
        (size_t)TEST_STRIDE, (size_t)TEST_SIZE);
    if (!(pool_data = malloc(sizeof(unsigned char *) * TEST_STRIDE))) {
        fprintf(stderr, "Out of memory!\n");
        abort();
    }
    for (size_t i = 0; i < TEST_STRIDE; i++) {
        if (!(pool_data[i] = malloc(TEST_SIZE))) {
            fprintf(stderr, "Out of memory!\n");
            abort();
        }
    }

    printf("Allocating %zu random memory pools of size %zu (bytes)\n",
        (size_t)TEST_STRIDE, (size_t)TEST_SIZE);
    // Read in some random which will be used to clear the memory on stride hits
    if (!(pool_random = malloc(sizeof(unsigned char *) * TEST_STRIDE))) {
        fprintf(stderr, "Out of memory!\n");
        abort();
    }

    printf("Populating random pools with entropy (this may take awhile)\n");
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open source for entropy (%s)\n",
            strerror(errno));
        abort();
    }

    for (size_t i = 0; i < TEST_STRIDE; i++) {
        if (!(pool_random[i] = malloc(TEST_SIZE))) {
            fprintf(stderr, "Out of memory!\n");
            abort();
        }
        read(fd, pool_random[i], TEST_SIZE);
    }
    close(fd);
    putchar('\n');
}

// Randomize the data in the pool to force commit and to prevent zero-page mapping
static void pool_randomize(void) {
    if (pool_random_index == TEST_STRIDE)
        pool_random_index = 0;
    for (size_t i = 0; i < TEST_STRIDE; i++)
        memcpy(pool_data[i], pool_random[pool_random_index++], TEST_SIZE);
}

static void *pool_alloc(void) {
    if (pool_data_index == TEST_STRIDE) {
        pool_randomize();
        pool_data_index = 0;
    }
    return pool_data[pool_data_index++];
}

/// Chunk of zero data
static unsigned char zero_data[TEST_SIZE];

/// The test functions
void NOINLINE test_memset_dispatch(unsigned char *data) {
    memset(data, 0, TEST_SIZE);
}

void NOINLINE test_memcpy_dispatch(unsigned char *data) {
    memcpy(data, zero_data, TEST_SIZE);
}

/// Indirect to prevent any sort of clever inlining or ifunc resolve
static void (*test_memset)(unsigned char *) = NULL;
static void (*test_memcpy)(unsigned char *) = NULL;

/// At runtime we set up the functions
static void __attribute__((constructor)) setup_test_functions() {
    test_memset = &test_memset_dispatch;
    test_memcpy = &test_memcpy_dispatch;
}

typedef struct {
    ull_t begin;
    ull_t end;
    ull_t iteration;
    ull_t difference;
} entry_t;

static void attempt(entry_t *entry, void (*function)(unsigned char*), ull_t i) {
    unsigned char *memory = pool_alloc();
    entry->iteration = i;

    /// We only time around the actual function
    entry->begin = tsc();
    function(memory);
    entry->end = tsc();

    /// Validate it (just incase)
    for (size_t i = 0; i < TEST_SIZE; i++) {
        if (memory[i] == 0)
            continue;
        fprintf(stderr, "Fatal error zeroing memory\n");
        abort();
    }
}

#define DO_MEMSET(ITER) attempt(&entries[(ITER)], test_memset, (ITER))
#define DO_MEMCPY(ITER) attempt(&entries[(ITER)], test_memcpy, (ITER))

/// Make the CPU do some work before beginning tests
static int spin_up_cpu(size_t m, size_t n) {
    if (m == 0) return n + 1;
    if (n == 0) return spin_up_cpu(m - 1, 1);
    return spin_up_cpu(m - 1, spin_up_cpu(m, n - 1));
}

static entry_t *test(void) {
    printf("Zeroing %zu (bytes) over %zu iterations (for a total of ",
        (size_t)TEST_SIZE, (size_t)TEST_ITERS);

    mpz_t total;
    mpz_init(total);
    mpz_set_ui(total, TEST_SIZE);
    mpz_mul_ui(total, total, TEST_ITERS);
    mpz_out_str(stdout, 10, total);
    printf(" bytes)\n\n");
    mpz_clear(total);

    entry_t *entries = calloc(TEST_ITERS, sizeof(entry_t));
    if (!entries) {
        fprintf(stderr, "Out of memory!\n");
        abort();
    }

    spin_up_cpu(CPU_SPINUP_FACTOR, CPU_SPINUP_FACTOR);
    for (size_t i = 1; i < TEST_ITERS + 1; i++) {
        DO_MEMSET(i - 1);
        printf("[MEMSET] %02d%%         \r", (100 * i) / TEST_ITERS);
        if (i % TEST_STRIDE == 0)
            printf("[CLEAR] \r");
    }
    printf("\r[MEMSET] Completed\n");

    spin_up_cpu(CPU_SPINUP_FACTOR, CPU_SPINUP_FACTOR);
    for (size_t i = 1; i < TEST_ITERS + 1; i++) {
        DO_MEMCPY(i - 1);
        printf("[MEMCPY] %02d%%         \r", (100 * i) / TEST_ITERS);
        if (i % TEST_STRIDE == 0)
            printf("[CLEAR] \r");
    }
    printf("\r[MEMCPY] Completed\n");

    return entries;
}

static ull_t calculate_difference(entry_t *entry) {
    long long ticks = 0;
    if (entry->end > entry->begin)
        ticks = (long long)(entry->end - entry->begin);
    else if (entry->begin > entry->end)
        ticks = (long long)(entry->begin - entry->end);
    if (ticks < 0)
        ticks = -ticks;
    return (ull_t)ticks;
}

static void print_stats(entry_t *entries) {
    printf("\nStatistics:\n");

    mpz_t average_memset;
    mpz_t average_memcpy;
    mpz_init(average_memset);
    mpz_init(average_memcpy);
    mpz_set_ui(average_memset, 0); /// average_memset = 0
    mpz_set_ui(average_memcpy, 0); /// average_memcpy = 0;

    ull_t smallest_memset = ~0ULL;
    ull_t smallest_memcpy = ~0ULL;
    ull_t largest_memset = 0;
    ull_t largest_memcpy = 0;

    /// Entries array contains bottom half memset while top half is memcpy
    for (size_t i = 0; i < TEST_ITERS / 2; i++) {
        entry_t *entry = &entries[i];
        const ull_t difference = calculate_difference(entry);
        if (difference > largest_memset) largest_memset = difference;
        if (difference < smallest_memset) smallest_memset = difference;
        entry->difference = difference;
        mpz_add_ui(average_memset, average_memset, difference);
    }
    for (size_t i = TEST_ITERS / 2; i < TEST_ITERS; i++) {
        entry_t *entry = &entries[i];
        const ull_t difference = calculate_difference(entry);
        if (difference > largest_memcpy) largest_memcpy = difference;
        if (difference < smallest_memcpy) smallest_memcpy = difference;
        entry->difference = difference;
        mpz_add_ui(average_memcpy, average_memcpy, difference);
    }
    mpz_div_ui(average_memset, average_memset, TEST_ITERS); ///average_memset /= TEST_ITERS
    mpz_div_ui(average_memcpy, average_memcpy, TEST_ITERS); ///average_memcpy /= TEST_ITERS

    printf("average memset took ");
    mpz_out_str(stdout, 10, average_memset);
    printf(" (%s)\n", TIME_STAMP_NAME);

    printf("average memcpy took ");
    mpz_out_str(stdout, 10, average_memcpy);
    printf(" (%s)\n", TIME_STAMP_NAME);

    mpz_clear(average_memset);
    mpz_clear(average_memcpy);

    printf("quickest memset took %llu (%s)\n", smallest_memset, TIME_STAMP_NAME);
    printf("quickest memcpy took %llu (%s)\n", smallest_memcpy, TIME_STAMP_NAME);

    printf("slowest memset took %llu (%s)\n", largest_memset, TIME_STAMP_NAME);
    printf("slowest memcpy took %llu (%s)\n", largest_memcpy, TIME_STAMP_NAME);
}

static void generate_graph(entry_t *entries) {
#ifdef USE_GNUPLOT
    char *data = tempnam("/tmp", "benchmark");
    char *script = tempnam("/tmp", "benchmark");

    FILE *datafp = fopen(data, "w");
    FILE *scriptfp = fopen(script, "w");

    for (size_t i = 0; i < TEST_ITERS / 2; i++) {
        fprintf(datafp, "%llu %llu\n",
            entries[i].difference,
            entries[i + (TEST_ITERS / 2)].difference
        );
    }
    fclose(datafp);

    fprintf(scriptfp,
        "set title \"memset vs memcpy\"\n"
        "set xlabel \"Iterations\"\n"
        "set ylabel \"Time (%s)\"\n"
        "set yrange [*:]\n"
        "set style data linespoints\n"
        "set terminal png size 1024,768\n"
        "set output 'output.png'\n"
        "plot \"%s\" using 0:1 with lines title 'memset', '' using 0:2 with lines title 'memcpy'\n",
        TIME_STAMP_NAME, data
    );
    fclose(scriptfp);

    unsigned char command[KB(1)];
    snprintf(command, sizeof(command), "gnuplot -e \"load '%s'\"", script);

    printf("\nGenerating graph\n");
    system(command);

    remove(data);
    remove(script);

    free(data);
    free(script);
    printf("\nSee 'output.png' for more details\n");
#endif
}

static void cleanup(entry_t *entries) {
    for (size_t i = 0; i < TEST_STRIDE; i++) {
        free(pool_data[i]);
        free(pool_random[i]);
    }
    free(pool_data);
    free(pool_random);
    free(entries);
}

int main(void) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    entry_t *entries = test();
    print_stats(entries);
    generate_graph(entries);
    cleanup(entries);
}
