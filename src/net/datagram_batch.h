#pragma once

// A buffer of UDP datagrams for one send, and the pool that recycles such buffers.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <xtensor/xtensor.hpp>

#include <coro/io/byte_buffer.h>

namespace net {

// xtensor's from_shape() leaves the bytes uninitialized: no zero-fill pass over a buffer about to be written in
// full. Its own begin()/end() are strided iterators, so DatagramBatch exposes raw pointers instead.
using BatchBytes = xt::xtensor<uint8_t, 1>;

class BatchPool;

// Datagrams laid out back to back, sent with one send. Only the first `used` bytes are sent. More than one
// datagram needs UDP GSO (enable_gso()): the kernel splits the buffer into datagrams of the socket's segment
// size, so they must all be that size, except that the last may be shorter.
//
// Move-only. A batch from BatchPool::acquire() hands its buffer back to the pool when destroyed, so whoever
// ends up with the batch recycles it by dropping it.
struct DatagramBatch {
    BatchBytes bytes;
    size_t used = 0;

    DatagramBatch() = default;
    // One datagram, all of `bytes`, in a buffer that isn't from a pool: it is freed once sent.
    explicit DatagramBatch(BatchBytes datagram) : bytes(std::move(datagram)), used(bytes.size()) {}
    DatagramBatch(DatagramBatch&&) noexcept = default;
    DatagramBatch& operator=(DatagramBatch&& other) noexcept;
    ~DatagramBatch() { recycle(); }

    uint8_t* begin() { return bytes.data(); }
    uint8_t* end() { return bytes.data() + used; }
    const uint8_t* begin() const { return bytes.data(); }
    const uint8_t* end() const { return bytes.data() + used; }

private:
    friend class BatchPool;
    void recycle() noexcept;

    // Null for a batch that isn't from a pool, and after a move (moving a shared_ptr nulls it).
    std::shared_ptr<BatchPool> m_pool;
};
static_assert(coro::ByteBuffer<DatagramBatch>);

// Recycles fixed-size batch buffers, so that a sender allocates only until it has as many as are ever in flight
// at once. Thread-safe: a batch may be acquired on one thread and dropped on another. Batches still in flight
// keep the pool alive after its creator has gone.
class BatchPool : public std::enable_shared_from_this<BatchPool> {
public:
    static std::shared_ptr<BatchPool> create(size_t buffer_bytes) {
        return std::shared_ptr<BatchPool>(new BatchPool(buffer_bytes));
    }

    // An empty batch (used == 0) with a buffer of buffer_bytes: a recycled one, or a new one.
    DatagramBatch acquire() {
        DatagramBatch batch;
        {
            std::lock_guard lock(m_mutex);
            if (!m_free.empty()) {
                batch.bytes = std::move(m_free.back());
                m_free.pop_back();
            }
        }
        // Allocated outside the lock.
        if (batch.bytes.size() == 0) batch.bytes = BatchBytes::from_shape({m_buffer_bytes});
        batch.m_pool = shared_from_this();
        return batch;
    }

private:
    friend struct DatagramBatch;

    explicit BatchPool(size_t buffer_bytes) : m_buffer_bytes(buffer_bytes) {}

    void release(BatchBytes bytes) noexcept {
        std::lock_guard lock(m_mutex);
        // Never more than were in flight at once, so the list needs no cap. If growing it throws, the
        // buffer is just freed.
        try {
            m_free.push_back(std::move(bytes));
        } catch (...) {
        }
    }

    const size_t m_buffer_bytes;
    std::mutex m_mutex;
    std::vector<BatchBytes> m_free;  // GUARDED BY m_mutex
};

inline DatagramBatch& DatagramBatch::operator=(DatagramBatch&& other) noexcept {
    if (this != &other) {
        recycle();
        bytes = std::move(other.bytes);
        used = other.used;
        m_pool = std::move(other.m_pool);
    }
    return *this;
}

inline void DatagramBatch::recycle() noexcept {
    if (m_pool) m_pool->release(std::move(bytes));
    m_pool.reset();
}

}  // namespace net
