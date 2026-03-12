#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

extern "C"
{
#include <libavutil/frame.h>
}

namespace transcoder {

/**
 * FrameBuffer - Thread-safe bounded queue between Decoder and Encoder threads.
 *
 * Implements the Producer-Consumer pattern:
 *   - push() blocks when queue is full   (decoder waits for encoder to catch up)
 *   - pop()  blocks when queue is empty  (encoder waits for decoder to produce)
 *   - shutdown() wakes all blocked threads for graceful exit
 *
 * Memory: frames pushed here are OWNED by the queue.
 *         The caller of pop() takes ownership and must call av_frame_free().
 */
class FrameBuffer {
public:
    explicit FrameBuffer(int maxSize = 30);
    ~FrameBuffer();

    // Push a frame. Blocks if queue is at maxSize.
    // Takes ownership of frame.
    // Returns false if shutdown was signaled.
    bool push(AVFrame* frame);

    // Pop a frame. Blocks if queue is empty.
    // Returns nullptr if shutdown was signaled and queue is empty.
    // Caller must call av_frame_free() on the returned frame.
    AVFrame* pop();

    // Signal all threads to stop waiting and return.
    // Call this before joining threads.
    void shutdown();

    // Current queue depth (approximate — not locked)
    int size() const;

    // True if shutdown() has been called
    bool isShutdown() const { return shutdown_.load(); }

private:
    std::queue<AVFrame*>     queue_;
    mutable std::mutex       mutex_;
    std::condition_variable  cvNotFull_;
    std::condition_variable  cvNotEmpty_;
    std::atomic<bool>        shutdown_ {false};
    const int                maxSize_;
};

} // namespace transcoder
