#include "frame_buffer.hpp"
#include <iostream>

namespace transcoder {

FrameBuffer::FrameBuffer(int maxSize): maxSize_(maxSize) {}

FrameBuffer::~FrameBuffer() {
    // Drain and free any remaining frames
    shutdown();
    std::unique_lock<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        AVFrame* f = queue_.front();
        queue_.pop();
        av_frame_free(&f);
    }
}

bool FrameBuffer::push(AVFrame* frame) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Block until there is space OR shutdown
    cvNotFull_.wait(lock, [this] {
        return static_cast<int>(queue_.size()) < maxSize_ || shutdown_.load();
    });

    if (shutdown_.load()) {
        // Don't accept new frames after shutdown — free it
        av_frame_free(&frame);
        return false;
    }

    queue_.push(frame);
    cvNotEmpty_.notify_one();
    return true;
}

AVFrame* FrameBuffer::pop() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Block until there is a frame OR (shutdown AND queue empty)
    cvNotEmpty_.wait(lock, [this] {
        return !queue_.empty() || shutdown_.load();
    });

    if (queue_.empty()) {
        // Shutdown was signaled and nothing left
        return nullptr;
    }

    AVFrame* frame = queue_.front();
    queue_.pop();
    cvNotFull_.notify_one();
    return frame;
}

void FrameBuffer::shutdown() {
    shutdown_.store(true);
    cvNotFull_.notify_all();
    cvNotEmpty_.notify_all();
}

int FrameBuffer::size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

} // namespace transcoder
