#ifndef R_BUFFER_HDR
#define R_BUFFER_HDR
#include "r_common.h"
#include "u_vector.h"

namespace r {

// Implements a variety of asynchronous buffer transfer techniques depending on
// the capabilities provided by the GPU.
//
// Essentially there is a few features used here if present:
//  GL_ARB_sync:
//      When working with manually pinned memory (MapBufferRange) we use
//      GL_UNSYNCRONIZED_BIT which provides the fastest possible mapping.
//      This leaves synchronization entirely up to us.
//
//      The technique implored here is to have as many fences as we have buffers
//      which is specified by `count'. This ensures we rarely wait (if at all.)
//      due to having multiple buffers in a chain.
//
//  GL_ARB_map_buffer_range:
//      Typically most operations on buffers are sub-range changes. Like writing
//      a vertex. We manipulate the data store in ranges, keeping a record of the
//      ranges modified so we can flush all the appropriate changes in chain. We
//      also try to find overlapping range changes and coalesce them into a single
//      flush if possible, reducing the amount of invocations to the driver.
//
//  MapBufferRange:
//      Mapping the memory into the client address space provides us with a raw
//      backing store to perform operations on. This eliminates the need to do
//      out-of-band copies into GPU memory.
//
// glMapBuffer on it's own provides no control over synchronization. As a result
// utilizing plain glBufferData copies provides similar performance characteristics
// as glMapBuffer on most devices. Because of this we don't utilize glMapBuffer
// at all and prefer the standard technique above all else.
struct buffer {
    buffer(size_t size, size_t count = 3);
    ~buffer();
    void init();

    // To control buffer index and mapping, we need to do manipulation within
    // a buffer scope which is indicated by a pair of the following functions.
    void beginChanges();
    void endChanges();
    // Similarly, after we've drawn with the buffer, we must post the changes
    // so that a fence can be inserted into the command queue indicatinh that the
    // fenced data is used in a draw operation
    void postChanges();

    // Write `data' of length `size' bytes at `offset' into the buffer.
    void write(unsigned char *data, size_t size, size_t offset);

protected:
    // Used to create and delete a mapping
    void createMapping(size_t bufferIndex);
    void deleteMapping(size_t bufferIndex);
private:
    // Stores to the backing buffer data provided by MapBufferRange calls need
    // explicit flushing of the changed sub-ranges. This is used to record those
    // ranges, deferring the flushes as late as possible.
    struct flushRecord { size_t offset, count; };
    u::vector<flushRecord> m_flushRecords;

    u::vector<GLuint> m_bufferObjects;
    u::vector<unsigned char *> m_bufferMappings;
    u::vector<GLsync> m_bufferFences;
    uint64_t m_bufferMappingBitset;

    size_t m_bufferSize;
    size_t m_bufferCount;
    size_t m_bufferIndex;
};

// Example use:
// r::buffer b(sizeof(m::vec3) * kMaxParticles);
// b.beginChanges();
// for (size_t i = 0; i < m_particles.size(); i++) {
//   b.write((unsigned char *)&m_particles[i],
//           sizeof(m::vec3),
//           sizeof(m::vec3) * it);
// }
// b.endChanges();
// gl::DrawElements(...);
// b.postChanges();
//

}

#endif
#include "r_buffer.h"

namespace r {

buffer::buffer(size_t size, size_t count)
    : m_bufferSize(size)
    , m_bufferCount(count)
    , m_bufferMappingBitset(0)
{
    // Only have so many bits to keep track of used buffer mappings
    assert(count <= sizeof m_bufferMappingBitset * CHAR_BIT);
    m_bufferObjects.resize(m_bufferCount);
    m_bufferMappings.resize(m_bufferCount);
}

buffer::~buffer() {
    if (gl::has(gl::ARB_map_buffer_range)) {
        if (gl::has(gl::ARB_sync))
            for (size_t i = 0; i < m_bufferFences.size(); i++)
                if (m_bufferFences[i])
                    gl::DeleteSync(it);
        for (size_t i = 0; i < m_bufferMappings.size(); i++)
            deleteMapping(i);
    } else {
        for (size_t i = 0; i < m_bufferMappings.size(); i++)
            neoFree(m_bufferMappings[i]);
    }
    gl::DeleteBuffers(m_bufferCount, &m_bufferObjects[0]);
}

void buffer::createMapping(size_t bufferIndex) {
    // Create a mapping for writing to only, hinting the driver that
    // we're responsible for flushing the writes and that we want the
    // driver to invalidate previous contents (we reuse these mappings.)
    // If ARB_sync is present, we also hint that we'd prefer to do
    // synchronization ourselfs.
    m_bufferMappings[bufferIndex] =
        gl::MapBufferRange(GL_ARRAY_BUFFER,
                           0,
                           m_size,
                           GL_MAP_WRITE_BIT |
                           GL_MAP_FLUSH_EXPLICIT_BIT |
                           GL_MAP_INVALIDATE_RANGE_BIT |
                           (gl::has(gl::ARB_sync)
                                ? GL_MAP_UNSYNCRONIZED_BIT
                                : 0));
    m_bufferMappingBitset |= (1 << bufferIndex);
}

void buffer::deleteMapping(size_t bufferIndex) {
    gl::UnmapBuffer(m_bufferMappings[bufferIndex]);
    m_bufferMappingBitset &= ~(1 << bufferIndex);
}

void buffer::init() {
    const bool manualSyncronization = gl::has(gl::ARB_sync);
    gl::GenBuffers(&m_bufferObjects[0], m_bufferCount);
    if (gl::has(gl::ARB_map_buffer_range)) {
        for (size_t i = 0; i < m_bufferObjects.size(); i++) {
            gl::BindBuffer(GL_ARRAY_BUFFER, m_bufferObjects[i]);
            createMapping(i);
        }
        // Upfront allocate fence objects if we're doing synchronization ourselfs.
        if (manualSyncronization)
            m_bufferFences.resize(m_bufferCount);
    } else {
        // If the extension is not present, we fallback to standard ping pong
        // technique and do an out-of-band copy of the data.
        for (size_t i = 0; i < m_bufferMappings.size(); i++) {
            m_bufferMappings[i] = neoMalloc(m_bufferSize);
            gl::BindBuffer(GL_ARRAY_BUFFER, m_bufferObjects[i]);
            gl::BufferData(GL_ARRAY_BUFFER, m_bufferSize, 0, GL_DYNAMIC_DRAW);
        }
    }
}

void buffer::beginChanges() {
    m_bufferIndex = m_bufferIndex++ % m_bufferCount;
    const bool manualSyncronization = gl::has(gl::ARB_sync);
    if (manualSyncronization) {
        // Wait until buffer is free to use, in most cases this should not wait
        // because we are using `m_bufferCount' buffers in a chain.
        GLenum result = gl::ClientWaitSync(m_fences[m_bufferIndex], 0, TIMEOUT);
        assert(result != GL_TIMEOUT_EXPIRED);
        assert(result != GL_WAIT_FAILED);
        gl::DeleteSync(m_bufferFences[m_bufferIndex]);
    }
    gl::BindBuffer(GL_ARRAY_BUFFER, m_bufferObjects[m_bufferIndex]);
    if (gl::has(gl::ARB_map_buffer_range)) {
        // If the buffer is no longer mapped, map it in
        if (!(m_bufferMappingBitset & (1 << m_bufferIndex)))
            createMapping(m_bufferIndex);
    }
}

void buffer::endChanges() {
    // Sort the flush records by offset
    u::sort(m_flushRecords.begin(), m_flushRecords.end(),
        [](const flushRecord &a, const flushRecord &b) {
            return a.offset < b.offset;
        });
    // Coalesce flush records to reduce the amount of driver invocations
    flushRecord &previousRecord = m_flushRecords[0];
    for (size_t i = 1; i < m_flushRecords.size(); i++) {
        flushRecord &currentRecord = m_flushRecords[i];
        if (currentRecord.offset == previousRecord.offset+previousRecord.count) {
            const size_t newRange = currentRecord.count - currentRecord.offset;
            // Erase the flush record and adjust the count of the previous
            // record so it reaches into the current record.
            m_flushRecords.erase(m_flushRecords.begin() + i,
                                 m_flushRecords.begin() + i + 1);
            m_previousRecord.count = newRange;
        }
        previousRecord = currentRecord;
    }
    if (gl::has(gl::ARB_map_buffer_range)) {
        // Flush the writes to memory with one flush after the other.
        for (auto &it : m_flushRecords)
            gl::FlushMappedBufferRange(GL_ARRAY_BUFFER, it.offset, it.count);
        deleteMapping(m_bufferIndex);
    } else {
        // Flush the changes using BufferSubData calls
        for (auto &it : m_flushRecords) {
            gl::BufferSubData(GL_ARRAY_BUFFER, it.offset, it.count,
                (const void *)&m_bufferMappings[m_bufferIndex] + it.offset);
        }
    }
    m_flushRecords.clear();
}

void buffer::postChanges() {
    // Insert a fence into the command queue. The next use of this buffer will
    // have to wait for the commands to be complete.
    if (gl::has(gl::ARB_sync))
        m_bufferFences[m_bufferIndex] = gl::FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void buffer::write(const void *const data, size_t size, size_t offset) {
    // Write the data
    unsigned char *destHead = m_bufferMappings[m_bufferIndex];
    const unsigned char *const destTail = destHead + m_bufferSize;
    assert(destHead + offset + size <= destTail);
    memcpy(destHead + offset, data, size);
    m_flushRecords.push_back({ offset, size });
}

}
