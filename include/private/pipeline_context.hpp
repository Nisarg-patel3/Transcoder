#pragma once

#include "transcoder.hpp"

#include <cstdint>

extern "C"
{
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace transcoder
{
    struct InputInfo
    {
        AVStream videoStream;
        AVStream audioStream;
        SourceType type;
        ContainerFormat container;
        VideoCodecType videoCodec;
        AudioCodecType audioCodec;
        bool hasAudioStream;
    };

    struct DecoderInfo
    {
        AVCodecContext videoDecoderCtx;
        AVCodecContext audioDecoderCtx;
    };

    struct EncoderInfo
    {
        AVCodecContext videoEncoderCtx;
        AVCodecContext audioEncoderCtx;
    };

    struct OutputInfo
    {
    };

    struct PipelineContext
    {
        InputInfo inputInfo;
        DecoderInfo decoderInfo;
        EncoderInfo encoderInfo;
        OutputInfo outputInfo;

        bool videoTranscodingRequired;
        bool audioTranscodingRequired;
    };
}