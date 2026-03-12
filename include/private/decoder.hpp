#pragma once

#include "pipeline_context.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <vector>

namespace transcoder
{

    class Decoder
    {
    public:
        virtual ~Decoder() = default;

        virtual bool initialize(PipelineContext pipelineCtx) = 0;

        virtual std::vector<AVFrame *> decodePacket(AVPacket *pkt) = 0;
        virtual std::vector<AVFrame *> flush() = 0;
    };

} // namespace transcoder