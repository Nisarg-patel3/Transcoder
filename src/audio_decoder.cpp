#include "audio_decoder.hpp"
#include <iostream>

namespace transcoder
{

    AudioDecoder::AudioDecoder() {}

    AudioDecoder::~AudioDecoder()
    {
        if (audioCodecCtx_)
        {
            avcodec_free_context(&audioCodecCtx_);
            audioCodecCtx_ = nullptr;
        }
    }

    bool AudioDecoder::initialize(PipelineContext pipelineCtx)
    {
        AVCodecID codecId = pipelineCtx.inputInfo.audioStream.codecpar->codec_id;
        const AVCodec *codec = avcodec_find_decoder(codecId);
        if (!codec)
        {
            std::cout << "[Audio Decoder] ERROR: No decoder found for codec id " << codecId << "\n";
            return false;
        }

        audioCodecCtx_ = avcodec_alloc_context3(codec);
        if (!audioCodecCtx_)
        {
            std::cout << "[Audio Decoder] ERROR: Failed to allocate codec context\n";
            return false;
        }

        int ret = avcodec_parameters_to_context(audioCodecCtx_, pipelineCtx.inputInfo.audioStream.codecpar);
        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Decoder] ERROR: Failed to copy codec params: " << err << "\n";
            return false;
        }

        audioCodecCtx_->pkt_timebase = pipelineCtx.inputInfo.audioStream.time_base;

        ret = avcodec_open2(audioCodecCtx_, codec, nullptr);
        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Decoder] ERROR: Failed to open codec: " << err << "\n";
            return false;
        }

        pipelineCtx.decoderInfo.audioDecoderCtx = (*audioCodecCtx_);

        std::cout << "[Audio Decoder] Initialized: " << codec->name << " | " 
                    << audioCodecCtx_->sample_rate << " Hz | "  
                    << av_q2d(audioCodecCtx_->pkt_timebase) << " tb | "  
                    << audioCodecCtx_->ch_layout.nb_channels << " channels | " 
                    << av_get_sample_fmt_name(audioCodecCtx_->sample_fmt) << " sample format | " 
                    << (audioCodecCtx_->bit_rate)/1000 << " kbps\n" ;

        return true;
    }

    std::vector<AVFrame *> AudioDecoder::decodePacket(AVPacket *pkt)
    {
        int ret = avcodec_send_packet(audioCodecCtx_, pkt);

        if (ret == AVERROR(EAGAIN))
        {
            // Must drain before sending more — should not happen normally
            return receiveFrames();
        }
        if (ret < 0 && ret != AVERROR_EOF)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Audio Decoder] Send packet error: " << err << "\n";
            return {};
        }

        return receiveFrames();
    }

    std::vector<AVFrame *> AudioDecoder::flush()
    {
        avcodec_send_packet(audioCodecCtx_, nullptr);
        return receiveFrames();
    }

    std::vector<AVFrame *> AudioDecoder::receiveFrames()
    {
        std::vector<AVFrame *> frames;

        while (true)
        {
            AVFrame *frame = av_frame_alloc();
            if (!frame)
                break;

            int ret = avcodec_receive_frame(audioCodecCtx_, frame);

            if (ret == 0)
            {
                frames.push_back(frame);
            }
            else
            {
                av_frame_free(&frame);
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    char err[256];
                    av_strerror(ret, err, sizeof(err));
                    std::cout << "[Audio Decoder] Receive frame error: " << err << "\n";
                }
                break;
            }
        }

        return frames;
    }

} // namespace transcoder