#include "video_decoder.hpp"

#include <iostream>

namespace transcoder {

VideoDecoder::~VideoDecoder()
{
    if (videoCodecCtx_)
    {
        avcodec_free_context(&videoCodecCtx_);
        videoCodecCtx_ = nullptr;
    }
}

bool VideoDecoder::initialize(PipelineContext pipelineCtx)
{
    AVCodecID codecId = pipelineCtx.inputInfo.videoStream.codecpar->codec_id;
    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec)
    {
        std::cout << "[Video Decoder] ERROR: No decoder found for codec id " << codecId << "\n";
        return false;
    }

    std::cout << codec->name << "\n";
    videoCodecCtx_ = avcodec_alloc_context3(codec);
    if (!videoCodecCtx_)
    {
        std::cout << "[Video Decoder] ERROR: Failed to allocate codec context\n";
        return false;
    }

    int ret = avcodec_parameters_to_context(videoCodecCtx_, pipelineCtx.inputInfo.videoStream.codecpar);
    if (ret < 0)
    {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cout << "[Video Decoder] ERROR: Failed to copy codec params: " << err << "\n";
        return false;
    }

    // Preserve frame rate info
    videoCodecCtx_->framerate    = pipelineCtx.inputInfo.videoStream.avg_frame_rate;
    videoCodecCtx_->pkt_timebase = pipelineCtx.inputInfo.videoStream.time_base;

    ret = avcodec_open2(videoCodecCtx_, codec, nullptr);
    if (ret < 0)
    {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cout << "[Video Decoder] ERROR: Failed to open codec: " << err << "\n";
        return false;
    }

    pipelineCtx.decoderInfo.videoDecoderCtx = (*videoCodecCtx_);


    std::cout << "[Video Decoder] Initialized: " << codec->name << " | " 
                << videoCodecCtx_->width << "x" << videoCodecCtx_->height << " | " 
                << av_q2d(videoCodecCtx_->framerate) << " fps | " 
                << av_q2d(videoCodecCtx_->pkt_timebase) << " tb | "  
                << (videoCodecCtx_->bit_rate)/1000 << " kbps\n";

    return true;
}

std::vector<AVFrame*> VideoDecoder::decodePacket(AVPacket* pkt)
{
    int ret = avcodec_send_packet(videoCodecCtx_, pkt);

    if (ret == AVERROR(EAGAIN))
    {
        // Must drain before sending more — should not happen normally
        return receiveFrames();
    }
    if (ret < 0 && ret != AVERROR_EOF)
    {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cout << "[Video Decoder] Send packet error: " << err << "\n";
        return {};
    }

    return receiveFrames();
}

std::vector<AVFrame*> VideoDecoder::flush()
{
    avcodec_send_packet(videoCodecCtx_, nullptr);
    return receiveFrames();
}

std::vector<AVFrame*> VideoDecoder::receiveFrames()
{
    std::vector<AVFrame*> frames;

    while (true)
    {
        AVFrame* frame = av_frame_alloc();
        if (!frame)
            break;

        int ret = avcodec_receive_frame(videoCodecCtx_, frame);

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
                std::cout << "[Video Decoder] Receive frame error: " << err << "\n";
            }
            break;
        }
    }

    return frames;
}
} // namespace transcoder