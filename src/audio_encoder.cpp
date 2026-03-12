#include "audio_encoder.hpp"
#include "transcoder.hpp"
#include "gpu_helper.hpp"

extern "C"
{
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <iostream>

namespace transcoder
{

    AudioEncoder::~AudioEncoder()
    {
        if (audioCodecCtx_)
        {
            avcodec_free_context(&audioCodecCtx_);
            audioCodecCtx_ = nullptr;
        }
    }

    bool AudioEncoder::initialize(const PipelineConfig &config, PipelineContext &pipelineCtx)
    {
        std::string name = getEncoderName(config.targetAudioCodec);

        const AVCodec *codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec)
        {
            std::cout << "[Audio Encoder] ERROR : Encoder Not found: " << name << "\n";
            return false;
        }

        audioCodecCtx_ = avcodec_alloc_context3(codec);

        // Sample rate
        audioCodecCtx_->sample_rate = pipelineCtx.inputInfo.audioStream.codecpar->sample_rate;
        // Check if this sample_rate is supported
        if (codec->supported_samplerates)
        {
            bool found = false;
            for (const int *sr = codec->supported_samplerates; *sr; ++sr)
            {
                if (*sr == pipelineCtx.inputInfo.audioStream.codecpar->sample_rate)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                audioCodecCtx_->sample_rate = codec->supported_samplerates[0];
        }
        audioCodecCtx_->time_base = {1, audioCodecCtx_->sample_rate};

        // Sample format: use the encoder's preferred format
        audioCodecCtx_->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : (AVSampleFormat)pipelineCtx.inputInfo.audioStream.codecpar->format;

        // Channel layout: keep the input layout
        av_channel_layout_copy(&audioCodecCtx_->ch_layout, &(pipelineCtx.inputInfo.audioStream.codecpar->ch_layout));

        audioCodecCtx_->bit_rate = pipelineCtx.inputInfo.audioStream.codecpar->bit_rate;
        // Bit rate: libmp3lame requires a non-zero bitrate
        if (audioCodecCtx_->bit_rate <= 0)
            audioCodecCtx_->bit_rate = 192000; // sensible default for MP3

        if (config.targetAudioCodec == AudioCodecType::MP3 &&
            audioCodecCtx_->ch_layout.nb_channels > 2)
        {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_uninit(&audioCodecCtx_->ch_layout);
            av_channel_layout_copy(&audioCodecCtx_->ch_layout, &stereo);
            std::cout << "[Audio Encoder] MP3 does not support >2 channels, downmixing to stereo\n";
        }

        int ret = avcodec_open2(audioCodecCtx_, codec, nullptr);

        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Encoder] ERROR : Open failed: " << err << "\n";
            return false;
        }

        pipelineCtx.encoderInfo.audioEncoderCtx = (*audioCodecCtx_);

        // TODO: Swr Context stuff
        ret = swr_alloc_set_opts2(&swrContext_,
                                  &audioCodecCtx_->ch_layout, audioCodecCtx_->sample_fmt, audioCodecCtx_->sample_rate,
                                  &(pipelineCtx.inputInfo.audioStream.codecpar->ch_layout),
                                  (AVSampleFormat)pipelineCtx.inputInfo.audioStream.codecpar->format,
                                  pipelineCtx.inputInfo.audioStream.codecpar->sample_rate,
                                  0, nullptr);
        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Encoder] ERROR: SwrContext allocation failed: " << err << "\n";
            return false;
        }

        ret = swr_init(swrContext_);
        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Encoder] ERROR: SwrContext initialization failed: " << err << "\n";
            return false;
        }

        std::cout << "[Audio Encoder] Initialized: " << codec->name << " | "
                  << audioCodecCtx_->sample_rate << " Hz | "
                  << av_q2d(audioCodecCtx_->pkt_timebase) << " tb | "
                  << audioCodecCtx_->ch_layout.nb_channels << " channels | "
                  << av_get_sample_fmt_name(audioCodecCtx_->sample_fmt) << " sample format | "
                  << (audioCodecCtx_->bit_rate) / 1000 << " kbps\n";

        return true;
    }

    std::vector<AVPacket *> AudioEncoder::encodeFrame(AVFrame *frame)
    {
        int ret = avcodec_send_frame(audioCodecCtx_, frame);

        if (ret < 0 && ret != AVERROR(EAGAIN))
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Encoder] Send frame error : " << err << "\n";
            return {};
        }
        return receivePackets();
    }

    std::vector<AVPacket *> AudioEncoder::flush()
    {
        avcodec_send_frame(audioCodecCtx_, nullptr);
        return receivePackets();
    }

    std::vector<AVPacket *> AudioEncoder::receivePackets()
    {
        std::vector<AVPacket *> out;
        while (true)
        {
            AVPacket *pkt = av_packet_alloc();
            int ret = avcodec_receive_packet(audioCodecCtx_, pkt);
            if (ret == 0)
            {
                out.push_back(pkt);
            }
            else
            {
                av_packet_free(&pkt);
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    char err[256];
                    av_strerror(ret, err, sizeof(err));
                    std::cout << "[Audio Encoder] ERROR: Receive packet error: " << err << "\n";
                }
                break;
            }
        }
        return out;
    }

    std::string AudioEncoder::getEncoderName(AudioCodecType codec)
    {
        switch (codec)
        {
        case AudioCodecType::AAC:
            return "aac";

        case AudioCodecType::MP3:
            return "libmp3lame";

        case AudioCodecType::AC3:
            return "ac3";

        case AudioCodecType::EAC3:
            return "eac3";

        case AudioCodecType::DTS:
            return "dca";

        case AudioCodecType::OPUS:
            return "libopus";

        case AudioCodecType::VORBIS:
            return "libvorbis";

        case AudioCodecType::PCM:
            return "pcm_s16le";

        case AudioCodecType::FLAC:
            return "flac";

        case AudioCodecType::ALAC:
            return "alac";

        case AudioCodecType::MP2:
            return "mp2";

        case AudioCodecType::SPEEX:
            return "libspeex";

        case AudioCodecType::WMAV2:
            return "wmav2";

        default:
            return "";
        }
    }
} // namespace transcoder
