#include "config_validator.hpp"
#include "transcoder.hpp"
#include "pipeline_context.hpp"

extern "C"
{
#include <libavformat/avformat.h>
}

#include <string>
#include <iostream>

namespace transcoder
{
    bool ConfigValidator::validateInputOutputPaths(const PipelineConfig &cfg)
    {
        bool ok = true;

        // ── 1. Input path & source type ───────────────────────────────────────
        if (cfg.inputPath.empty())
        {
            std::cout << "\t[Validator] Input path is empty. Provide a file path or stream URL.\n";
            ok = false;
        }
        else if (cfg.sourceType == SourceType::FILE && isStreamUrl(cfg.inputPath))
        {
            std::cout << "\t[Validator] ERROR: Source type is FILE but input path looks like a stream URL: '" << cfg.inputPath + "'. Set sourceType = SourceType::STREAM.\n";
            ok = false;
        }
        else if (cfg.sourceType == SourceType::FILE && detectContainerFromFilePath(cfg.inputPath) == ContainerFormat::UNKNOWN)
        {
            std::cout << "\t[Validator] ERROR: Cannot determine container format from input path '" << cfg.inputPath + "'. Use a recognised extension: " << listSupportedExtensions() << "\n";
            ok = false;
        }
        else if (cfg.sourceType == SourceType::RTSP && !isStreamUrl(cfg.inputPath))
        {
            std::cout << "\t[Validator] ERROR: Source type is STREAM but input path does not look like a stream URL: '" << cfg.inputPath + "'. Expected rtsp://, rtsps://\n";
            ok = false;
        }

        // ── 2. Output path & container detection ──────────────────────────────
        if (cfg.outputPath.empty())
        {
            std::cout << "\t[Validator] ERROR: Output path is empty. Provide a destination file path.\n";
            ok = false;
        }
        else if (detectContainerFromFilePath(cfg.outputPath) == ContainerFormat::UNKNOWN)
        {
            std::cout << "\t[Validator] ERROR: Cannot determine container format from outputPath '" << cfg.outputPath + "'. Use a recognised extension: " << listSupportedExtensions() << "\n";
            ok = false;
        }

        return ok;
    }

    bool ConfigValidator::validateConfiguration(PipelineConfig &cfg, PipelineContext& pipelineCtx)
    {
        bool ok = true;

        std::cout << "[Validator] Validating pipeline configurations.........\n";

        // Fetch info from input
        InputInfo inputInfo = pipelineCtx.inputInfo;
        ContainerFormat container = detectContainerFromFilePath(cfg.outputPath);

        // ── Video codec ↔ container ────────────────────────────────────────
        if (cfg.keepOriginalVideoCodec)
            cfg.targetVideoCodec = inputInfo.videoCodec;

        bool sourceAndTargetCodecSame = (cfg.targetVideoCodec == inputInfo.videoCodec);

        pipelineCtx.videoTranscodingRequired = !(sourceAndTargetCodecSame && !cfg.enableProcessing && cfg.keepOriginalFramerate && cfg.keepOriginalBitrate && !cfg.segmentEnabled);

        if (pipelineCtx.videoTranscodingRequired && inputInfo.videoCodec == VideoCodecType::UNKNOWN)
        {
            std::cout << "\t[ERROR] Video transcoding is required, but input file's video codec is unknown or not supported. Supported video codecs are: " << listSupportedVideoCodecs() << "\n";
            ok = false;
        }

        if (pipelineCtx.videoTranscodingRequired && cfg.targetVideoCodec == VideoCodecType::UNKNOWN)
        {
            std::cout << "\t[ERROR] Video transcoding is required, but target video codec is unknown or not supported. Set a valid target video codec. Supported video codecs are: " << listSupportedVideoCodecs() << "\n";
            ok = false;
        }

        if (!ok)
            return false;

        if (!isVideoCodecSupportedInContainer(cfg.targetVideoCodec, container))
        {
            std::cout << "\t[WARNING] " << "Video codec '" << videoCodecTypeToString(cfg.targetVideoCodec, true) << "' is generally not supported in the '" << containerFormatToString(container) << "' container. Recommended video codecs for " << containerFormatToString(container) << ": " << listSupportedVideoCodecs(container) << ".\n";
        }

        // ── Audio codec ↔ container ────────────────────────────────────────

        pipelineCtx.audioTranscodingRequired = cfg.passAudio && inputInfo.hasAudioStream && (cfg.targetAudioCodec != inputInfo.audioCodec || cfg.segmentEnabled);

        if (cfg.passAudio)
        {
            if (!cfg.keepOriginalAudioCodec && !inputInfo.hasAudioStream)
            {
                std::cout << "\t[ERROR] Target audio codec is set but input doesn't have a audio stream.\n";
                ok = false;
            }
            else
            {
                if (cfg.keepOriginalAudioCodec && inputInfo.hasAudioStream)
                    cfg.targetAudioCodec = inputInfo.audioCodec;

                if (pipelineCtx.audioTranscodingRequired && inputInfo.audioCodec == AudioCodecType::UNKNOWN)
                {
                    std::cout << "\t[ERROR] Audio transcoding is required, but input file's audio codec is unknown or not supported. Supported audio codecs are: " << listSupportedAudioCodecs() << "\n";
                    ok = false;
                }

                if (pipelineCtx.audioTranscodingRequired && cfg.targetAudioCodec == AudioCodecType::UNKNOWN)
                {
                    std::cout << "\t[ERROR] Audio transcoding is required, but target audio codec is unknown or not supported. Set a valid target audio codec. Supported audio codecs are: " << listSupportedAudioCodecs() << "\n";
                    ok = false;
                }

                if (!ok)
                    return false;

                if (!isAudioCodecSupportedInContainer(cfg.targetAudioCodec, container))
                {
                    std::cout << "\t[WARNING] " << "Audio codec '" << audioCodecTypeToString(cfg.targetAudioCodec, true) << "' is generally not supported in the '" << containerFormatToString(container) << "' container. Recommended audio codecs for " << containerFormatToString(container) << ": " << listSupportedAudioCodecs(container) << ".\n";
                }
            }
        }

        // ── Compatibility suggestions/warnings ──────────────────────────────
        if (cfg.passAudio && inputInfo.hasAudioStream)
        {
            checkCompat(cfg, container, inputInfo);
        }

        // ── Bitrate / CRF ──────────────────────────────────────────────────
        if(cfg.keepOriginalBitrate)
        {
            if(inputInfo.videoStream.codecpar->bit_rate<=0)
            {
                std::cout << "\t[WARNING] " << "keepOriginalBitrate is true, but input doesn't have bitrate information. Setting it to default (CRF=23)\n";
                cfg.bitrateMode = BitrateMode::CQP;
                cfg.crf = 23;
            }
            else 
            {
                cfg.bitrateMode = BitrateMode::VBR;
                cfg.targetBitrate = inputInfo.videoStream.codecpar->bit_rate;
            }
        }
        else
        {
            if (cfg.bitrateMode == BitrateMode::CBR || cfg.bitrateMode == BitrateMode::VBR)
            {
                if (cfg.targetBitrate <= 0)
                {
                    std::cout << "\t[ERROR] " << bitrateModeToString(cfg.bitrateMode) << " mode requires a positive targetBitrate (bits/sec). Got: " << std::to_string(cfg.targetBitrate) << ". Example: 4000000 = 4 Mbps.\n";
                    ok = false;
                }
                else if (cfg.targetBitrate < 50'000)
                {
                    std::cout << "\t[WARNING] targetBitrate is very low (" << std::to_string(cfg.targetBitrate / 1000) << " kbps). Video quality may be extremely poor.\n";
                }
                else if(cfg.targetBitrate > 16'000'000)
                {
                    std::cout << "\t[WARNING] targetBitrate is very high (" << std::to_string(cfg.targetBitrate / 1000) << " kbps). Video file size may be extremely large.\n";
                }
            }
            else
            { // CQP
                if (cfg.crf < 0 || cfg.crf > 51)
                {
                    std::cout << "\t[ERROR] CQP mode requires crf in range 0–51 (0 = best quality, 51 = worst). Got: " << std::to_string(cfg.crf) << ".\n";
                    ok = false;
                }
            }
        }

        // ── 7. Framerate ──────────────────────────────────────────────────────
        if (cfg.keepOriginalFramerate)
        {
            if(inputInfo.videoStream.avg_frame_rate.num<=0)
            {
                std::cout << "\t[WARNING] " << "keepOriginalFramerate is true, but input doesn't have framerate information. Setting it to default (30 fps)\n";
                cfg.targetFramerate = 30;
            }
            else 
            {
               cfg.targetFramerate = inputInfo.videoStream.avg_frame_rate.num;
            }
        }
        else 
        {
            if (cfg.targetFramerate <= 0)
            {
                std::cout << "\t[ERROR] keepOriginalFramerate is false but targetFramerate = " << std::to_string(cfg.targetFramerate) << ". Must be a positive integer (e.g. 24, 30, 60).\n";
                ok = false;
            }
            else if (cfg.targetFramerate > 240)
            {
                std::cout << "\t[WARNING] targetFramerate = " << std::to_string(cfg.targetFramerate) << " is unusually high. Typical values are 24, 25, 30, 60.\n";
            }

            if (cfg.targetFramerate > inputInfo.videoStream.avg_frame_rate.num)
            {
                std::cout << "\t[WARNING] Target framerate is higher than original framerate, video will speed up.\n";
            }
            else if(cfg.targetFramerate < inputInfo.videoStream.avg_frame_rate.num)
            {
                std::cout << "\t[WARNING] Target framerate is lower than original framerate, video will slow down.\n";
            }
        }

        // ── 8. Queue size ─────────────────────────────────────────────────────
        if (cfg.queueMaxSize <= 0)
        {
            std::cout << "\t[ERROR] Queue size must be a positive integer. Got: " << std::to_string(cfg.queueMaxSize) << ". Recommended range: 100–500.\n";
            ok = false;
        }
        else if (cfg.queueMaxSize > 2000)
        {
            std::cout << "\t[WARNING] Queue size = " << std::to_string(cfg.queueMaxSize) << " may cause excessive memory usage (each frame can be several MB). Typical range: 100–500.\n";
        }

        // ── 9. Stats ──────────────────────────────────────────────────────────
        if (cfg.enableStats)
        {
            if (cfg.statsInterval <= 0)
            {
                std::cout << "\t[ERROR] Stats are enabled but interval = " << std::to_string(cfg.statsInterval) << ". Must be a positive number of seconds.\n";
                ok = false;
            }
            else if (cfg.statsInterval > 60)
            {
                std::cout << "\t[WARNING] Stat interval = " << std::to_string(cfg.statsInterval) << "s is very long. Consider 1–5s for timely feedback.\n";
            }
        }

        // ── 10. Stream-only options ───────────────────────────────────────────
        if(cfg.sourceType==SourceType::RTSP && !cfg.segmentEnabled)
        {
            std::cout << "\t[ERROR] Input is a stream, but video segmentation is not enabled\n";
            ok = false;
        }
        else if (cfg.segmentEnabled)
        {
            if (cfg.segmentDurationSeconds <= 0)
            {
                std::cout << "\t[ERROR] Video segmentation is enabled but, segment duration = " << std::to_string(cfg.segmentDurationSeconds) << ". Must be positive (seconds per output file chunk). Typical range: 10–60.\n";
                ok = false;
            }
            else if (cfg.segmentDurationSeconds < 2)
            {
                std::cout << "\t[WARNING] Segment duration = " << std::to_string(cfg.segmentDurationSeconds) << "s produces very short chunks,causing excessive file I/O and keyframe overhead.\n";
            }
        }

         std::cout << "[Validator] Validation complete.........\n";

        return ok;
    }

    bool ConfigValidator::checkCompat(const PipelineConfig &cfg, ContainerFormat container, InputInfo inputInfo)
    {
        const VideoCodecType vCodec = cfg.targetVideoCodec;
        const AudioCodecType aCodec = cfg.targetAudioCodec;

        // Warning related to output container
        if (cfg.sourceType == SourceType::RTSP && container != ContainerFormat::MPEG_TS && container != ContainerFormat::MKV)
        {
            std::cout << "\t[WARNING] Input is a live stream but output container is '" << containerFormatToString(container) << "'. MPEG-TS (.ts) or MKV (.mkv) is strongly recommended for streaming output — they tolerate abrupt stops and partial writes.\n";
        }

        // MJPEG is an intra-only codec (no inter-frame compression).
        // Opus and Vorbis are designed for use alongside modern inter-frame codecs
        // and cannot be muxed with MJPEG in any container this library supports.
        if (vCodec == VideoCodecType::MJPEG)
        {
            if (aCodec == AudioCodecType::OPUS || aCodec == AudioCodecType::VORBIS)
            {
               std::cout << "\t[WARNING]  " << "MJPEG video + " << audioCodecTypeToString(aCodec, true) << " audio is not a valid combination. " << audioCodecTypeToString(aCodec) << " cannot be muxed alongside MJPEG. Use AAC, MP3, or PCM with MJPEG.\n";
            }
            else if (aCodec == AudioCodecType::AC3)
            {
                std::cout << "\t[WARNING] " << "MJPEG + AC3 is technically valid but uncommon. AAC or PCM are the conventional choices for MJPEG output.\n";
            }
        }

        // VP9 in MKV — AC3 works but almost no player treats such a file as
        // standard. Opus is the natural pairing for VP9.
        if (vCodec == VideoCodecType::VP9 && container == ContainerFormat::MKV && aCodec == AudioCodecType::AC3)
        {
            std::cout << "\t[WARNING] " << "VP9 + AC3 in MKV is technically valid but uncommon. Opus or AAC are the conventional choices for VP9 content.\n";
        }

        // AAC in AVI: FFmpeg can technically write it but virtually no player
        // reads AAC from an AVI file correctly. Treated as a hard error.
        if (container == ContainerFormat::AVI && aCodec == AudioCodecType::AAC)
        {
           std::cout << "\t[WARNING] " << "AAC audio is not supported in AVI. AVI supports: MP3, PCM, AC3. Switch to MP4 or MKV if you need AAC audio.\n";
        }

        return true;
    }
}