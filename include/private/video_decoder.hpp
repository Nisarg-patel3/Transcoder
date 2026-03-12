#pragma once

#include "decoder.hpp"

extern "C"
{
#include <libavutil/rational.h>
}

namespace transcoder
{

    class VideoDecoder : public Decoder
    {
    public:
        VideoDecoder() = default;
        ~VideoDecoder() override;

        bool initialize(PipelineContext PipelineCtx) override;

        std::vector<AVFrame *> decodePacket(AVPacket *pkt) override;
        std::vector<AVFrame *> flush() override;
    
    private:
        std::vector<AVFrame *> receiveFrames();

        AVCodecContext *videoCodecCtx_ = nullptr;
    };

} // namespace transcoder