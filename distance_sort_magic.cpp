//
// Efficient camera distance sorting technique for renderers that need to sort
// objects by distance to camera: front-to-back (transparent) or back-to-front
// (occlusion).
//
// This works on the principal of an 11-bit radix sort which has a state that
// fits entirely in L1 cache (3 11-bit histograms are used.)
//
// Benchmarks:
//  Note: all testing was done with a sort key at position 0 in the object
//        (for cache locality.) & objects exactly 64 bytes in size.
//
// Performance on a low end mobile Intel:
//  Sorts ~6356992 objects/s
// Performance on my Phenom II X6:
//  Sorts ~30267164 objects/s
//
//
// Single precision floats cannot be radix sorted directly as negative numbers
// turn out bigger than positive ones. In a similar nature, values are
// sign-magnituded, as a result more negative numbers tend to be bigger to
// normal comparisons.
//
// This code is memory-bound.
//
// Code is placed in the public domain
// By: Dale Weiler
//
#include <stdint.h> // uint32_t
#include <stddef.h> // size_t
#include <string.h> // memcpy

static inline uint32_t flip(uint32_t f) {
    const uint32_t mask = -int32_t(f >> 31) | 0x80000000u;
    return f ^ mask;
}

// Some utilities for reading 11-bit quantities
#define AT0(X) ((X) & 0x7FF)
#define AT1(X) ((X) >> 11 & 0x7FF)
#define AT2(X) ((X) >> 22)

// This sorting function is destructive. It clobbers the key at keyOffset in
// each object, as well as the original array.
template <typename T, size_t keyOffset>
static void sort(T *contents, T *sorted, size_t elements) {
    // Get the key in the contents
    #define KEY(WHERE, I) *((uint32_t*)((unsigned char *)&((WHERE)[I]) + keyOffset))

    // Histogram with statically defined block offsets
    const uint32_t kHist = 2048;
    uint32_t hist[kHist * 3] = {0},
            *b0 = hist,
            *b1 = b0 + kHist,
            *b2 = b1 + kHist;

    // Calculate histogram in parallel
    for (size_t i = 0; i < elements; i++) {
        const uint32_t reduce = flip(KEY(contents, i));
        b0[AT0(reduce)]++;
        b1[AT1(reduce)]++;
        b2[AT2(reduce)]++;
        __builtin_prefetch(&contents[i + 1], 0, 1);
    }

    // Sum histograms in parallel
    uint32_t sum0 = 0, sum0j = 0,
             sum1 = 0, sum1j = 0,
             sum2 = 0, sum2j = 0;
    for (size_t i = 0;i < kHist; i++) {
        sum0j = b0[i] + sum0, b0[i] = sum0 - 1, sum0 = sum0j;
        sum1j = b1[i] + sum1, b1[i] = sum1 - 1, sum1 = sum1j;
        sum2j = b2[i] + sum2, b2[i] = sum2 - 1, sum2 = sum2j;
        __builtin_prefetch(&b0[i + 1], 1, 1);
        __builtin_prefetch(&b1[i + 1], 1, 1);
        __builtin_prefetch(&b2[i + 1], 1, 1);
    }

    // Now radix sort the contents
    for (size_t i = 0; i < elements; i++) {
        const uint32_t reduce = flip(KEY(contents, i));
        KEY(contents, i) = reduce;
        const uint32_t position = AT0(reduce);
        const size_t index = ++b0[position];
        memcpy(&sorted[index], &contents[i], sizeof(T));
    }
    for (size_t i = 0; i < elements; i++) {
        const uint32_t reduce = KEY(sorted, i);
        const uint32_t position = AT1(reduce);
        const size_t index = ++b1[position];
        memcpy(&contents[index], &sorted[i], sizeof(T));
    }
    for (size_t i = 0; i < elements; i++) {
        const uint32_t reduce = KEY(contents, i);
        const uint32_t position = AT2(reduce);
        const size_t index = ++b2[position];
        memcpy(&sorted[index], &contents[i], sizeof(T));
    }
}

#if 0
// Example of use
struct particle {
    vec3 position;
    float sortkey;
    /// ... other attributes
}

// each frame
vector<particle> particles;
for (auto &p : particles)
    p.sortkey = (p.position - camera).abs();

// sort them
vector<particle> scratch;
scratch.resize(particles.size());
sort<particle, offsetof(particle, sortkey)>(&particles[0], &scratch[0], particles.size());
// scratch now contains sorted particles
// particles is a clobbered mess

// DONE!
#endif

#include <stdio.h>
struct test {
    int index;
    float key;
};

static test contents[] = {
    { 3 , 3.14 },
    { 2 , 1.0 },
    { 4 , 100.5 },
    { 1 , 0.8 }
};

int main() {
    #define size (sizeof contents / sizeof *contents)
    test sorted[size];
    sort<test, offsetof(test, key)>(contents, sorted, size);
    for (size_t i = 0; i < size; i++)
        printf("%d\n", sorted[i].index);
}
