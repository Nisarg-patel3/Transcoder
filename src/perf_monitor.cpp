#include "transcoder.hpp"
#include <iostream>
#include <iomanip>

namespace transcoder {

PerfMonitor::PerfMonitor() {
    reset();
}

void PerfMonitor::reset() {
    perSecDecoded_ = 0;
    perSecEncoded_ = 0;
    perSecBytes_   = 0;
    totalDecoded_  = 0;
    totalEncoded_  = 0;
    totalBytes_    = 0;
    latencyUs_     = 0;
    queueDepth_    = 0;
    startTime_     = std::chrono::steady_clock::now();
}

void PerfMonitor::recordFrameDecoded() {
    perSecDecoded_.fetch_add(1);
    totalDecoded_.fetch_add(1);
}

void PerfMonitor::recordFrameEncoded(int bytesWritten) {
    perSecEncoded_.fetch_add(1);
    totalEncoded_.fetch_add(1);
    perSecBytes_.fetch_add(bytesWritten);
    totalBytes_.fetch_add(bytesWritten);
}

void PerfMonitor::recordLatency(int64_t us) {
    latencyUs_.store(us);
}

void PerfMonitor::updateQueueDepth(int depth) {
    queueDepth_.store(depth);
}

void PerfMonitor::printStats() {
    // Snapshot and reset per-second counters atomically
    int     fps      = perSecEncoded_.exchange(0);
    int     decoded  = perSecDecoded_.exchange(0);
    int64_t bytes    = perSecBytes_.exchange(0);
    int64_t latUs    = latencyUs_.load();
    int     qDepth   = queueDepth_.load();
    int64_t totEnc   = totalEncoded_.load();

    double kbps      = (bytes * 8.0) / 1000.0;
    double latMs     = latUs / 1000.0;

    auto   now       = std::chrono::steady_clock::now();
    double elapsed   = std::chrono::duration<double>(now - startTime_).count();

    std::cout
        << "[" << std::fixed << std::setprecision(1) << elapsed << "s] "
        << "Enc: "     << std::setw(4) << fps
        << " | Dec: "  << std::setw(4) << decoded
        << " | Bitrate: " << std::setw(8) << std::setprecision(1) << kbps << " kbps"
        << " | Queue: " << std::setw(2) << qDepth
        << " | Latency: " << std::setw(6) << std::setprecision(2) << latMs << " ms"
        << " | Total: " << totEnc << " frames"
        << std::endl;
}

} // namespace transcoder
