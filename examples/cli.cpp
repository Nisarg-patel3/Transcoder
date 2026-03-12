/**
 * transcode_cli.cpp
 * =================
 * Command-line interface for the Video Transcoding Pipeline Library.
 *
 * Usage:
 *   transcode_cli -i <input> -o <output> [options]
 *
 * Run with --help for full usage.
 */

#include <transcoder.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>

using namespace transcoder;

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static void printHelp(const char *prog)
{

    std::string videoCodecs = "";
    for (int i = (int)VideoCodecType::FIRST; i <= (int)VideoCodecType::LAST; i++)
    {
        videoCodecs += videoCodecTypeToString((VideoCodecType)i) + ", ";
    }
    std::string audioCodecs = "";
    for (int i = (int)AudioCodecType::FIRST; i <= (int)AudioCodecType::LAST; i++)
    {
        audioCodecs += audioCodecTypeToString((AudioCodecType)i) + ", ";
    }

    std::cout <<
        R"(Video Transcoding Pipeline CLI

Usage:
  )" << prog << R"( -i <input> -o <output> [options]

Mandatory:
  -i, --input  <path|url>     Input file path or RTSP stream URL.
  -o, --output <path>         Output file path.

Codec options (optional – original codec kept if not specified):
  --video-codec  <codec>      Target video codec.
                              Values: )"
              << videoCodecs << R"(
  --audio-codec  <codec>      Target audio codec.
                              Values: )"
              << audioCodecs << R"(
                              
Framerate (optional):
  --fps <n>                   Target framerate (integer, e.g. 30).

Bitrate (optional – specify mode first, then value):
  --bitrate-mode <mode>       Bitrate mode: cbr | vbr | cqp
  --bitrate <bps>             Target bitrate in bits/s. Required for cbr / vbr.
                              Example: 3000000  (= 3 Mbps)
  --crf <0-51>                CRF quality value. Required for cqp mode.
                              0 = best quality, 51 = worst.

Buffer:
  --queue-size <n>            Max frames in flight between threads (default 300).

Audio:
  --no-audio                  Discard audio stream (do not pass through).

Stats:
  --no-stats                  Disable live statistics output.
  --stats-interval <secs>     Seconds between stats prints (default 1).
                              Ignored when --no-stats is set.

Streaming:
  --slice-interval <secs>     Stream slicing interval in seconds (default 30).
                              Only meaningful when input is an RTSP stream.

Misc:
  -h, --help                  Show this help message and exit.

Examples:
  # Simple H.264 → H.265 file transcode at 3 Mbps VBR
  transcode_cli -i input.mp4 -o output.mp4 --video-codec h265 \
                --bitrate-mode vbr --bitrate 3000000

  # Transcode RTSP stream, slice every 60 s, no audio
  transcode_cli -i rtsp://camera.local/stream -o segment.mp4 \
                --video-codec h264 --no-audio --slice-interval 60

  # CQP / CRF encode, keep original audio and framerate
  transcode_cli -i input.mkv -o output.mkv --video-codec vp9 \
                --bitrate-mode cqp --crf 18
)";
}

// ──────────────────────────────────────────────────────────────────────────────
// String → enum converters
// ──────────────────────────────────────────────────────────────────────────────

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static VideoCodecType parseVideoCodec(const std::string &s)
{
    const auto l = toLower(s);

    for (int i = (int)VideoCodecType::FIRST; i <= (int)VideoCodecType::LAST; i++)
    {
        if (videoCodecTypeToString((VideoCodecType)i) == s)
            return (VideoCodecType)i;
    }
    throw std::invalid_argument("Unknown video codec: " + s + "\n");
}

static AudioCodecType parseAudioCodec(const std::string &s)
{
    const auto l = toLower(s);
    for (int i = (int)AudioCodecType::FIRST; i <= (int)AudioCodecType::LAST; i++)
    {
        if (audioCodecTypeToString((AudioCodecType)i) == s)
            return (AudioCodecType)i;
    }

    throw std::invalid_argument("Unknown audio codec: " + s + "\n");
}

static BitrateMode parseBitrateMode(const std::string &s)
{
    const auto l = toLower(s);
    if (l == "cbr")
        return BitrateMode::CBR;
    if (l == "vbr")
        return BitrateMode::VBR;
    if (l == "cqp" || l == "crf")
        return BitrateMode::CQP;
    throw std::invalid_argument("Unknown bitrate mode: " + s + "\n");
}

static bool isRtspUrl(const std::string &path)
{
    const auto l = toLower(path);
    return l.rfind("rtsp://", 0) == 0 || l.rfind("rtsps://", 0) == 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Argument parser state
// ──────────────────────────────────────────────────────────────────────────────

struct ParsedArgs
{
    // mandatory
    std::string inputPath;
    std::string outputPath;

    // codec
    bool setVideoCodec = false;
    VideoCodecType videoCodec = VideoCodecType::H264;

    bool setAudioCodec = false;
    AudioCodecType audioCodec = AudioCodecType::AAC;

    // framerate
    bool setFps = false;
    int fps = 30;

    // bitrate
    bool setBitrateMode = false;
    BitrateMode bitrateMode = BitrateMode::CBR;
    bool setBitrate = false;
    int bitrate = 4'000'000;
    bool setCrf = false;
    int crf = 23;

    // queue
    bool setQueueSize = false;
    int queueSize = 300;

    // audio/stats
    bool noAudio = false;
    bool noStats = false;
    bool setStatsInterval = false;
    int statsInterval = 1;

    // stream slicing
    bool setSliceInterval = false;
    int sliceInterval = 30;

    HWDeviceType hwDevice = HWDeviceType::NONE;
};

// ──────────────────────────────────────────────────────────────────────────────
// Parse argv
// ──────────────────────────────────────────────────────────────────────────────

static ParsedArgs parseArgs(int argc, char *argv[])
{
    ParsedArgs args;

    auto nextArg = [&](int &i, const char *flag) -> std::string
    {
        if (i + 1 >= argc)
            throw std::invalid_argument(std::string(flag) + " requires a value.");
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i)
    {
        std::string_view a = argv[i];

        if (a == "-h" || a == "--help")
        {
            printHelp(argv[0]);
            std::exit(0);
        }
        else if (a == "-i" || a == "--input")
        {
            args.inputPath = nextArg(i, argv[i]);
        }
        else if (a == "-o" || a == "--output")
        {
            args.outputPath = nextArg(i, argv[i]);
        }
        else if (a == "--video-codec")
        {
            args.setVideoCodec = true;
            args.videoCodec = parseVideoCodec(nextArg(i, argv[i]));
        }
        else if (a == "--audio-codec")
        {
            args.audioCodec = parseAudioCodec(nextArg(i, argv[i]));
            args.setAudioCodec = true;
        }
        else if (a == "--fps")
        {
            args.fps = std::stoi(nextArg(i, argv[i]));
            args.setFps = true;
            if (args.fps <= 0)
                throw std::invalid_argument("--fps must be a positive integer.");
        }
        else if (a == "--bitrate-mode")
        {
            args.bitrateMode = parseBitrateMode(nextArg(i, argv[i]));
            args.setBitrateMode = true;
        }
        else if (a == "--bitrate")
        {
            args.bitrate = std::stoi(nextArg(i, argv[i]));
            args.setBitrate = true;
            if (args.bitrate <= 0)
                throw std::invalid_argument("--bitrate must be positive.");
        }
        else if (a == "--crf")
        {
            args.crf = std::stoi(nextArg(i, argv[i]));
            args.setCrf = true;
            if (args.crf < 0 || args.crf > 51)
                throw std::invalid_argument("--crf must be in range 0–51.");
        }
        else if (a == "--queue-size")
        {
            args.queueSize = std::stoi(nextArg(i, argv[i]));
            args.setQueueSize = true;
            if (args.queueSize <= 0)
                throw std::invalid_argument("--queue-size must be positive.");
        }
        else if (a == "--no-audio")
        {
            args.noAudio = true;
        }
        else if (a == "--no-stats")
        {
            args.noStats = true;
        }
        else if (a == "--stats-interval")
        {
            args.statsInterval = std::stoi(nextArg(i, argv[i]));
            args.setStatsInterval = true;
            if (args.statsInterval <= 0)
                throw std::invalid_argument("--stats-interval must be positive.");
        }
        else if (a == "--segment-enabled")
        {
            args.sliceInterval = std::stoi(nextArg(i, argv[i]));
            args.setSliceInterval = true;
            if (args.sliceInterval <= 0)
                throw std::invalid_argument("--segment-interval must be positive.");
        }
        else if(a == "--vaapi")
        {
            args.hwDevice = HWDeviceType::VAAPI;
        }
        else
        {
            throw std::invalid_argument("Unknown option: " + std::string(a) +
                                        "\nRun with --help for usage.");
        }
    }

    // ── Mandatory checks ────────────────────────────────────────────────────
    if (args.inputPath.empty())
        throw std::invalid_argument("Input path is required (-i / --input).");
    if (args.outputPath.empty())
        throw std::invalid_argument("Output path is required (-o / --output).");

    // ── Bitrate consistency checks ───────────────────────────────────────────
    if (args.setBitrateMode)
    {
        if ((args.bitrateMode == BitrateMode::CBR ||
             args.bitrateMode == BitrateMode::VBR) &&
            !args.setBitrate)
        {
            throw std::invalid_argument(
                "CBR and VBR modes require --bitrate to be specified.");
        }
        if (args.bitrateMode == BitrateMode::CQP && !args.setCrf)
        {
            throw std::invalid_argument(
                "CQP mode requires --crf to be specified.");
        }
    }

    return args;
}

// ──────────────────────────────────────────────────────────────────────────────
// Build PipelineConfig from parsed args
// ──────────────────────────────────────────────────────────────────────────────

static PipelineConfig buildConfig(const ParsedArgs &args)
{
    PipelineConfig cfg;

    // Input
    cfg.inputPath = args.inputPath;
    cfg.sourceType = isRtspUrl(args.inputPath) ? SourceType::RTSP : SourceType::FILE;

    // Output
    cfg.outputPath = args.outputPath;

    // Video codec
    if (args.setVideoCodec)
    {
        cfg.keepOriginalVideoCodec = false;
        cfg.targetVideoCodec = args.videoCodec;
    }

    // Audio codec
    if (args.setAudioCodec)
    {
        cfg.keepOriginalAudioCodec = false;
        cfg.targetAudioCodec = args.audioCodec;
    }

    // Framerate
    if (args.setFps)
    {
        cfg.keepOriginalFramerate = false;
        cfg.targetFramerate = args.fps;
    }

    // Bitrate
    if (args.setBitrateMode)
    {
        cfg.keepOriginalBitrate = false;
        cfg.bitrateMode = args.bitrateMode;

        if (args.bitrateMode == BitrateMode::CBR ||
            args.bitrateMode == BitrateMode::VBR)
        {
            cfg.targetBitrate = args.bitrate;
        }
        else
        { // CQP
            cfg.crf = args.crf;
        }
    }

    // Queue
    if (args.setQueueSize)
        cfg.queueMaxSize = args.queueSize;

    // Audio passthrough
    if (args.noAudio)
        cfg.passAudio = false;

    // Stats
    if (args.noStats)
    {
        cfg.enableStats = false;
    }
    else if (args.setStatsInterval)
    {
        cfg.statsInterval = args.statsInterval;
    }

    // Stream slicing (only meaningful for stream input, but we set it regardless
    // so the pipeline can decide; warn the user if it seems odd)
    if (args.setSliceInterval)
    {
        cfg.segmentDurationSeconds = args.sliceInterval;
        cfg.segmentEnabled = true;
        if (cfg.sourceType == SourceType::FILE)
        {
            std::cout << "[warn] --slice-interval is set but input is a file, "
                         "not an RTSP stream. The option may have no effect.\n";
        }
    }

    cfg.hwDeviceType = args.hwDevice;

    return cfg;
}

static void printConfig(const PipelineConfig &cfg)
{
    std::cout << "\n=== Pipeline Configuration ===\n";
    std::cout << "  Source type   : " << sourceTypeToString(cfg.sourceType) << "\n";
    std::cout << "  Input         : " << cfg.inputPath << "\n";
    std::cout << "  Output        : " << cfg.outputPath << "\n";

    if (cfg.keepOriginalVideoCodec)
        std::cout << "  Video codec   : keep original\n";
    else
        std::cout << "  Video codec   : " << videoCodecTypeToString(cfg.targetVideoCodec) << "\n";

    if (cfg.passAudio)
    {
        if (cfg.keepOriginalAudioCodec)
            std::cout << "  Audio codec   : keep original\n";
        else
            std::cout << "  Audio codec   : " << audioCodecTypeToString(cfg.targetAudioCodec) << "\n";
    }
    else
    {
        std::cout << "  Audio         : disabled\n";
    }

    if (cfg.keepOriginalFramerate)
        std::cout << "  Framerate     : keep original\n";
    else
        std::cout << "  Framerate     : " << cfg.targetFramerate << " fps\n";

    if (cfg.keepOriginalBitrate)
    {
        std::cout << "  Bitrate       : keep original\n";
    }
    else
    {
        std::cout << "  Bitrate mode  : " << bitrateModeToString(cfg.bitrateMode) << "\n";
        if (cfg.bitrateMode == BitrateMode::CQP)
            std::cout << "  CRF           : " << cfg.crf << "\n";
        else
            std::cout << "  Bitrate       : " << cfg.targetBitrate << " bps\n";
    }

    std::cout << "  Queue size    : " << cfg.queueMaxSize << " frames\n";

    if (cfg.enableStats)
        std::cout << "  Stats         : enabled (every " << cfg.statsInterval << "s)\n";
    else
        std::cout << "  Stats         : disabled\n";

    if (cfg.segmentEnabled)
        std::cout << "  Slice interval: " << cfg.segmentDurationSeconds << "s\n";

    std::cout << "==============================\n\n";
}

// ──────────────────────────────────────────────────────────────────────────────
// main
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        printHelp(argv[0]);
        return 1;
    }

    ParsedArgs args;
    try
    {
        args = parseArgs(argc, argv);
    }
    catch (const std::exception &ex)
    {
        std::cout << "[error] " << ex.what() << "\n";
        std::cout << "Run '" << argv[0] << " --help' for usage.\n";
        return 1;
    }

    PipelineConfig config = buildConfig(args);

    printConfig(config);

    // ── Build & run pipeline ──────────────────────────────────────────────────
    Pipeline pipeline(config);

    if (!pipeline.initialize())
    {
        std::cout << "[error] Failed to initialize pipeline.\n";
        return 1;
    }

    std::cout << "Transcoding: " << config.inputPath << " → " << config.outputPath << "\n";

    const bool ok = pipeline.run();

    if (ok && config.enableStats)
    {
        auto *stats = pipeline.getPerfMonitor();
        std::cout << "\n[done] Encoded " << stats->totalFramesEncoded() << " frames, " << stats->totalBytesOutput() / 1024 / 1024 << " MB output\n";
    }
    else if (!ok)
    {
        std::cout << "[error] Transcoding failed.\n";
    }

    return ok ? 0 : 1;
}