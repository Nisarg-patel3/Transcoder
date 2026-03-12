#pragma once

#include "transcoder.hpp"
#include "pipeline_context.hpp"

extern "C"
{
#include <libavutil/frame.h>
#include <libavcodec/packet.h>
}

#include <vector>

namespace transcoder
{

    class Encoder
    {
    public:
        virtual ~Encoder() = default;

        virtual bool initialize(const PipelineConfig &config, PipelineContext& pipelineCtx) = 0;

        virtual std::vector<AVPacket *> encodeFrame(AVFrame *frame) = 0;
        virtual std::vector<AVPacket *> flush() = 0;
    };

} // namespace transcoder
