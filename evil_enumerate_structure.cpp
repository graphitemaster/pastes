#include <stdint.h>
#include <stddef.h>

template <bool B, typename T = void>
struct enable_if { };
template <typename T>
struct enable_if<true, T> {
    typedef T type;
};

template <size_t totalSize, typename T>
constexpr size_t calcSize(size_t offset) {
    return totalSize >= sizeof(T) ? (totalSize - offset) / sizeof(T) : 0;
}

// This clever technique lets us make nice packed structures in MSVC
// using the __pragma keyword.
#if defined(_MSC_VER)
#   define PACKED_STRUCT(NAME) \
    __pragma(pack(1)) struct NAME
#else
#   define PACKED_STRUCT(NAME) \
    struct __attribute__((packed)) NAME
#endif

template <size_t totalSize, typename T, size_t offset>
PACKED_STRUCT(rawArray) {
    typedef T value_type[calcSize<totalSize, T>(offset)];
    unsigned char padding[offset];
    value_type value;
};

template <size_t totalSize, typename T, size_t offset = 0, typename = void>
union offsetCollection;
template <size_t totalSize, typename T, size_t offset>
union offsetCollection<totalSize, T, offset,
    typename enable_if<calcSize<totalSize, T>(offset) == 0>::type> { };
template <size_t totalSize, typename T, size_t offset>
union offsetCollection<totalSize, T, offset,
    typename enable_if<calcSize<totalSize, T>(offset) != 0 && offset == sizeof(T)-1>::type>
{
private:
    typedef rawArray<totalSize, T, offset> offset_type;
    typedef typename offset_type::value_type value_type;
    offset_type currentOffset;
public:
    template <size_t nextOffset = 0,
        typename enable_if<nextOffset == offset, int>::type = 0>
    T (&get())[calcSize<totalSize, T>(nextOffset)] {
        return *(value_type*)&currentOffset.value;
    }
};
template <size_t totalSize, typename T, size_t offset>
union offsetCollection<totalSize, T, offset,
    typename enable_if<calcSize<totalSize, T>(offset) != 0 && (offset < sizeof(T) - 1)>::type>
{
private:
    typedef rawArray<totalSize, T, offset> offset_type;
    typedef typename offset_type::value_type value_type;
    typedef offsetCollection<totalSize, T, offset+1> next_offset_type;
    offset_type currentOffset;
    next_offset_type nextOffset;
public:
    template <size_t nextOffset = 0,
        typename enable_if<nextOffset == offset, int>::type = 0>
    T (&get())[calcSize<totalSize, T>(nextOffset)] {
        return *(value_type*)&currentOffset.value;
    }
    template <size_t nextOffset = 0,
        typename enable_if<(nextOffset > offset), int>::type = 0>
    T (&get())[calcSize<totalSize, T>(nextOffset)] {
        return nextOffset.get<nextOffset>();
    }
};

template <typename T>
union enumerateAccess {
    offsetCollection<sizeof(T), uint8_t> asBytes;
    offsetCollection<sizeof(T), uint16_t> asWords;
    offsetCollection<sizeof(T), uint32_t> asDoubleWords;
    offsetCollection<sizeof(T), uint64_t> asQuadWords;
};

template <typename T>
enumerateAccess<T> &enumerate(T &value) {
    return *(enumerateAccess<T>*)&value;
}

#include <stdio.h>
int main() {
    struct { uint32_t x, y, z; } data = { 1, 2, 3 };
    printf("DOUBLE WORDS:\n");
    for (uint32_t &i : enumerate(data).asDoubleWords.get()) {
        printf("%d\n", i);
    }
    printf("WORDS:\n");
    for (uint16_t &i : enumerate(data).asWords.get()) {
        printf("%d\n", int(i));
    }
}
