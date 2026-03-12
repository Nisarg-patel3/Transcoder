#include "input_souce.hpp"
#include "transcoder.hpp"

extern "C"
{
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
}

#include <iostream>

namespace transcoder
{
    InputSource::~InputSource()
    {
        if (fmtCtx_)
            avformat_close_input(&fmtCtx_);
    }

    bool InputSource::initialize(const PipelineConfig &cfg, PipelineContext &pipelineCtx)
    {
        inputType_ = cfg.sourceType;
        inputPath_ = cfg.inputPath;

        if (!openInput())
            return false;

        if (!findStreams())
            return false;

        InputInfo inputInfo;
        inputInfo.type = cfg.sourceType;
        if (inputInfo.type == SourceType::FILE)
        {
            inputInfo.container = detectContainerFormat();
        }
        inputInfo.videoCodec = detectVideoCodecFromStream();
        inputInfo.videoStream = (*videoStream_);

        if (!audioStream_)
        {
            inputInfo.hasAudioStream = false;
        }
        else
        {
            inputInfo.hasAudioStream = true;
            inputInfo.audioCodec = detectAudioCodecFromStream();
            inputInfo.audioStream = *(audioStream_);
        }

        pipelineCtx.inputInfo = inputInfo;

        logInputInfo(inputInfo);

        return true;
    }

    bool InputSource::openInput()
    {
        AVDictionary *opts = nullptr;

        if (inputType_ == SourceType::RTSP)
        {
            av_dict_set(&opts, "rtsp_transport", "tcp", 0);
            av_dict_set(&opts, "stimeout", "5000000", 0); // 5s timeout
        }

        int ret = avformat_open_input(&fmtCtx_, inputPath_.c_str(), nullptr, &opts);
        av_dict_free(&opts);

        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Input Source] ERROR: Cannot open input source '" << inputPath_ << "': " << err << "\n";
            return false;
        }

        return true;
    }

    bool InputSource::findStreams()
    {
        int ret = avformat_find_stream_info(fmtCtx_, nullptr);
        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Input Source] ERROR: Cannot find stream info: " << err << "\n";
            return false;
        }

        for (unsigned i = 0; i < fmtCtx_->nb_streams; i++)
        {
            AVMediaType type = fmtCtx_->streams[i]->codecpar->codec_type;
            if (type == AVMEDIA_TYPE_VIDEO)
                videoStream_ = fmtCtx_->streams[i];
            else if (type == AVMEDIA_TYPE_AUDIO)
                audioStream_ = fmtCtx_->streams[i];
        }

        if (!videoStream_)
        {
            std::cout << "[Input Source] ERROR: No video stream found\n";
            return false;
        }

        if (!audioStream_)
        {
            std::cout << "[Input Source] WARNING: No audio stream found\n";
        }
        return true;
    }

    ContainerFormat InputSource::detectContainerFormat()
    {
        if (!fmtCtx_ || !fmtCtx_->iformat)
            return ContainerFormat::UNKNOWN;
        std::string name = fmtCtx_->iformat->name;

        // MP4 / MOV share same demuxer: "mov,mp4,m4a,3gp,3g2,mj2"
        if (name.find("mp4") != std::string::npos)
            return ContainerFormat::MP4;

        else if (name.find("mov") != std::string::npos)
            return ContainerFormat::MOV;

        // MKV / WebM
        else if (name.find("matroska") != std::string::npos ||
                 name.find("webm") != std::string::npos)
            return ContainerFormat::MKV;

        else if (name.find("avi") != std::string::npos)
            return ContainerFormat::AVI;

        else if (name.find("mpegts") != std::string::npos)
            return ContainerFormat::MPEG_TS;

        else if (name.find("flv") != std::string::npos)
            return ContainerFormat::FLV;

        return ContainerFormat::UNKNOWN;
    }

    VideoCodecType InputSource::detectVideoCodecFromStream()
    {
        if (!videoStream_)
            return VideoCodecType::UNKNOWN;

        switch (videoStream_->codecpar->codec_id)
        {
        case AV_CODEC_ID_H264:
            return VideoCodecType::H264;

        case AV_CODEC_ID_HEVC: // H.265
            return VideoCodecType::H265;

        case AV_CODEC_ID_MJPEG:
            return VideoCodecType::MJPEG;

        case AV_CODEC_ID_MPEG4:
            return VideoCodecType::MPEG4;

        case AV_CODEC_ID_VP9:
            return VideoCodecType::VP9;

        case AV_CODEC_ID_AV1:
            return VideoCodecType::AV1;

        default:
            return VideoCodecType::UNKNOWN;
        }
    }

    AudioCodecType InputSource::detectAudioCodecFromStream()
    {
        if (!audioStream_)
            return AudioCodecType::UNKNOWN;

        switch (audioStream_->codecpar->codec_id)
        {
        case AV_CODEC_ID_AAC:
            return AudioCodecType::AAC;

        case AV_CODEC_ID_MP3:
            return AudioCodecType::MP3;

        case AV_CODEC_ID_AC3:
            return AudioCodecType::AC3;

        case AV_CODEC_ID_EAC3:
            return AudioCodecType::EAC3;

        case AV_CODEC_ID_DTS:
            return AudioCodecType::DTS;

        case AV_CODEC_ID_OPUS:
            return AudioCodecType::OPUS;

        case AV_CODEC_ID_VORBIS:
            return AudioCodecType::VORBIS;

        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_S24LE:
        case AV_CODEC_ID_PCM_S24BE:
        case AV_CODEC_ID_PCM_S32LE:
        case AV_CODEC_ID_PCM_S32BE:
        case AV_CODEC_ID_PCM_F32LE:
        case AV_CODEC_ID_PCM_F32BE:
        case AV_CODEC_ID_PCM_F64LE:
        case AV_CODEC_ID_PCM_F64BE:
            return AudioCodecType::PCM;

        case AV_CODEC_ID_FLAC:
            return AudioCodecType::FLAC;

        case AV_CODEC_ID_ALAC:
            return AudioCodecType::ALAC;

        case AV_CODEC_ID_MP2:
            return AudioCodecType::MP2;

        case AV_CODEC_ID_SPEEX:
            return AudioCodecType::SPEEX;

        case AV_CODEC_ID_WMAV2:
            return AudioCodecType::WMAV2;

        default:
            return AudioCodecType::UNKNOWN;
        }
    }

    bool InputSource::readPacket(AVPacket *pkt)
    {
        while (true)
        {
            int ret = av_read_frame(fmtCtx_, pkt);

            if (ret == AVERROR_EOF)
                return false;

            if (ret == AVERROR(EAGAIN))
            {
                // No data yet — retry (common for live streams)
                continue;
            }

            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Input Source] ERROR: Read packet error: " << err << "\n";
                return false;
            }

            // Only return video and audio packets
            if (pkt->stream_index == videoStream_->index || pkt->stream_index == audioStream_->index)
            {
                return true;
            }

            // Skip subtitle/data/other streams
            av_packet_unref(pkt);
        }
    }

    void InputSource::logInputInfo(InputInfo inputInfo)
    {
        std::cout << "\n[Input Source] Opened : " << inputPath_ << "\n";

        if (inputType_ == SourceType::FILE)
        {
            std::cout << "\t Container: " << containerFormatToString(inputInfo.container) << "\n";
        }
        else
        {
            std::cout << "\t Stream: RSTP\n";
        }

        std::cout << "\t Video: " << videoCodecTypeToString(inputInfo.videoCodec, true) << " | "
                  << inputInfo.videoStream.codecpar->width << "x" << inputInfo.videoStream.codecpar->height << " | "
                  << av_q2d(inputInfo.videoStream.avg_frame_rate) << " fps | "
                  << av_q2d(inputInfo.videoStream.time_base) << " tb | "
                  << (inputInfo.videoStream.codecpar->bit_rate) / 1000 << " kbps\n";

        if (inputInfo.hasAudioStream)
        {
            std::cout << "\t Audio: " << audioCodecTypeToString(inputInfo.audioCodec, true) << " | "
                      << inputInfo.audioStream.codecpar->sample_rate << " Hz | "
                      << av_q2d(inputInfo.audioStream.time_base) << " tb | "
                      << inputInfo.audioStream.codecpar->ch_layout.nb_channels << " channels | "
                      << av_get_sample_fmt_name((AVSampleFormat)inputInfo.audioStream.codecpar->format) << " sample format | "
                      << (inputInfo.audioStream.codecpar->bit_rate) / 1000 << " kbps\n";
        }
        else
        {
            std::cout << "\t Audio: No audio\n";
        }

        return;
    }

    bool InputSource::isVideoPacket(const AVPacket *pkt) const
    {
        return pkt && pkt->stream_index == videoStream_->index;
    }

    bool InputSource::isAudioPacket(const AVPacket *pkt) const
    {
        return pkt && audioStream_ && pkt->stream_index == audioStream_->index;
    }
}