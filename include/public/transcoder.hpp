#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

extern "C"
{
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

namespace transcoder
{
    // ─────────────────────────────────────────────────────────────
    // Enumerations
    // ─────────────────────────────────────────────────────────────

    enum class VideoCodecType
    {
        UNKNOWN,
        FIRST,
        H264 = FIRST,
        H265,
        MJPEG,
        MPEG4,
        VP9,
        AV1,
        LAST = AV1,
    };

    enum class AudioCodecType
    {
        UNKNOWN,
        FIRST,
        AAC = FIRST,
        MP3,
        AC3,
        EAC3,
        DTS,
        OPUS,
        VORBIS,
        PCM,
        FLAC,
        ALAC,
        MP2,
        SPEEX,
        WMAV2,
        LAST = WMAV2,
    };

    enum class ContainerFormat
    {
        UNKNOWN,
        FIRST,
        MP4 = FIRST,
        MKV,
        AVI,
        MPEG_TS,
        FLV,
        MOV,
        LAST = MOV,
    };

    enum class BitrateMode
    {
        CBR, // Constant Bitrate
        VBR, // Variable Bitrate
        CQP, // Constant Quality
    };

    enum class SourceType
    {
        FILE,
        RTSP,
    };

    enum class HWDeviceType
    {
        NONE,  // CPU only (software encoding)
        VAAPI, // Intel GPU
    };

    /**
     * PipelineConfig - All settings needed to construct and run a Pipeline.
     * Pass this to Pipeline constructor.
     */
    struct PipelineConfig
    {

        // ── Input ────────────────────────────────────────────────
        std::string inputPath; // file path or RTSP URL
        SourceType sourceType = SourceType::FILE;

        // ── Output ───────────────────────────────────────────────
        std::string outputPath; // output file path

        // ── Target codecs ─────────────────────────────────────────
        bool keepOriginalVideoCodec = true;
        VideoCodecType targetVideoCodec = VideoCodecType::H264; // Used if above bool is false

        bool keepOriginalAudioCodec = true;
        AudioCodecType targetAudioCodec = AudioCodecType::AAC; // Used if above bool is false

        // ── Framerate control ──────────────────────────────────────
        bool keepOriginalFramerate = true;
        int targetFramerate = 30; // Used if above bool is false

        // ── Bitrate control ──────────────────────────────────────
        bool keepOriginalBitrate = true;

        // Used if above bool is false
        BitrateMode bitrateMode = BitrateMode::CBR;
        int targetBitrate = 4'000'000; // bits per second (4 Mbps default)
        int crf = 23;                  // Used with CQP mode, quality for CQP mode (0=best, 51=worst)

        // ── Frame buffer ─────────────────────────────────────────
        int queueMaxSize = 300; // max frames in flight between threads

        // ── Audio ────────────────────────────────────────────────
        bool passAudio = true; // keep audio or not

        // ── Performance ──────────────────────────────────────────
        bool enableStats = true; // print live stats every second
        int statsInterval = 1;   // seconds between stats prints

        // ── Enable a custom processing stage ──────────────────────────────────────────
        bool enableProcessing = false;

        // ── Enable video/stream segmentation ──────────────────────────────────────────
        bool segmentEnabled = false;
        int segmentDurationSeconds = 10;
        int64_t segmentFrameCount = 0;

        // ── Enable GPU encoding ──────────────────────────────────────────
        HWDeviceType hwDeviceType = HWDeviceType::NONE;
    };

    // Forward declarations
    struct PipelineContext;
    class InputSource;
    class Decoder;
    class FrameBuffer;
    class ProcessingStage;
    class PixelFormatConverter;
    class Encoder;
    class AudioEncoder;
    class OutputSink;
    class PerfMonitor;

    /**
     * Pipeline - Top-level orchestrator for the transcoding pipeline.
     */
    class Pipeline
    {
    public:
        explicit Pipeline(const PipelineConfig &config);
        ~Pipeline();

        // Set a custom processing stage before calling initialize()
        void setProcessingStage(std::unique_ptr<ProcessingStage> stage);

        // Initialize all components. Must be called before run() or start().
        // Returns false if any component fails to initialize.
        bool initialize();

        // Run the pipeline synchronously — blocks until transcoding is complete.
        bool run();

        // Start pipeline asynchronously — returns immediately.
        bool start();

        // Request pipeline to stop (non-blocking).
        void stop();

        // Wait for all threads to finish after stop() or natural completion.
        void wait();

        // True if pipeline is currently running
        bool isRunning() const { return running_.load(); }

        // Access the performance monitor for custom stats reading
        PerfMonitor *getPerfMonitor() const { return monitor_.get(); }

    private:
        void decoderLoop();
        void encoderLoop();
        void monitorLoop();

        bool initInputSource();
        bool initDecoder();
        bool initEncoder();
        bool initOutputSink(const std::unique_ptr<OutputSink>& output_);

        PipelineConfig config_;
        PipelineContext *context_;

        std::unique_ptr<InputSource> input_;
        std::unique_ptr<Decoder> videoDecoder_;
        std::unique_ptr<Decoder> audioDecoder_;
        std::unique_ptr<ProcessingStage> processor_;
        std::unique_ptr<FrameBuffer> buffer_;
        std::unique_ptr<Encoder> videoEncoder_;
        std::unique_ptr<AudioEncoder> audioEncoder_;
        std::vector<std::unique_ptr<OutputSink>> outputs_;
        std::unique_ptr<PerfMonitor> monitor_;

        std::thread decoderThread_;
        std::thread encoderThread_;
        std::thread monitorThread_;

        std::atomic<bool> running_{false};
        std::atomic<bool> decoderDone_{false};
        std::atomic<bool> encodeError_{false};
    };

    // ─────────────────────────────────────────────────────────────
    // Helper functions
    // ─────────────────────────────────────────────────────────────

    std::string videoCodecTypeToString(VideoCodecType c, bool pretty = false);
    std::string audioCodecTypeToString(AudioCodecType c, bool pretty = false);
    std::string containerFormatToString(ContainerFormat f);
    std::string containerFormatToExtension(ContainerFormat f);
    std::string sourceTypeToString(SourceType t);
    std::string bitrateModeToString(BitrateMode m);
    std::string hwDeviceTypeToString(HWDeviceType t);

    std::string listSupportedExtensions();
    std::string listSupportedVideoCodecs();
    std::string listSupportedVideoCodecs(ContainerFormat container);
    std::string listSupportedAudioCodecs();
    std::string listSupportedAudioCodecs(ContainerFormat container);

    bool isStreamUrl(const std::string &p);

    ContainerFormat detectContainerFromFilePath(const std::string &filePath);

    bool isVideoCodecSupportedInContainer(VideoCodecType codec, ContainerFormat container);
    bool isAudioCodecSupportedInContainer(AudioCodecType codec, ContainerFormat container);

    /**
     * PerfMonitor - Thread-safe performance statistics collector.
     *
     * Decoder/Encoder threads call record*() methods.
     * Monitor thread calls printStats() every N seconds.
     * All counters are atomic — no locking required.
     */
    class PerfMonitor
    {
    public:
        PerfMonitor();
        ~PerfMonitor() = default;

        // Called by decoder thread
        void recordFrameDecoded();

        // Called by encoder thread
        void recordFrameEncoded(int bytesWritten);

        // Called by encoder thread — microseconds between push and pop
        void recordLatency(int64_t us);

        // Called by encoder thread
        void updateQueueDepth(int depth);

        // Called by monitor thread — prints and resets per-second counters
        void printStats();

        // Reset all counters (called at start)
        void reset();

        // Accessors for external use
        int64_t totalFramesDecoded() const { return totalDecoded_.load(); }
        int64_t totalFramesEncoded() const { return totalEncoded_.load(); }
        int64_t totalBytesOutput() const { return totalBytes_.load(); }

    private:
        // Per-second counters (reset each printStats() call)
        std::atomic<int> perSecDecoded_{0};
        std::atomic<int> perSecEncoded_{0};
        std::atomic<int64_t> perSecBytes_{0};

        // Running totals
        std::atomic<int64_t> totalDecoded_{0};
        std::atomic<int64_t> totalEncoded_{0};
        std::atomic<int64_t> totalBytes_{0};

        // Latency (latest sample)
        std::atomic<int64_t> latencyUs_{0};

        // Queue depth (latest sample)
        std::atomic<int> queueDepth_{0};

        // Session start time (for elapsed time display)
        std::chrono::steady_clock::time_point startTime_;
    };

} // namespace transcoder
