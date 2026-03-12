#pragma once

#include "decoder.hpp"

extern "C"
{
#include <libavutil/samplefmt.h>
}

namespace transcoder
{

    class AudioDecoder : public Decoder
    {
    public:
        AudioDecoder();
        ~AudioDecoder() override;

        bool initialize(PipelineContext pipelineCtx) override;

        std::vector<AVFrame *> decodePacket(AVPacket *pkt) override;
        std::vector<AVFrame *> flush() override;

    private:
        std::vector<AVFrame *> receiveFrames();

        AVCodecContext *audioCodecCtx_ = nullptr;
    };

} // namespace transcoder