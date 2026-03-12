#pragma once

#include "transcoder.hpp"
#include "pipeline_context.hpp"
#include "encoder.hpp"

extern "C"
{

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/rational.h>
#include <libswresample/swresample.h>
}

#include <vector>
#include <memory>

namespace transcoder
{
    class AudioEncoder : public Encoder
    {
    public:
        AudioEncoder() = default;
        ~AudioEncoder() override;

        bool initialize(const PipelineConfig &config, PipelineContext &pipelineCtx) override;

        std::vector<AVPacket *> encodeFrame(AVFrame *frame) override;
        std::vector<AVPacket *> flush() override;

        SwrContext *getSwrContext() const { return swrContext_; }

    private:
        std::string getEncoderName(AudioCodecType codec);

        std::vector<AVPacket *> receivePackets();

        AVCodecContext *audioCodecCtx_ = nullptr;
        SwrContext *swrContext_ = nullptr;
    };

} // namespace transcoder
