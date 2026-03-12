#pragma once

#include "transcoder.hpp"
#include "pipeline_context.hpp"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
}

namespace transcoder
{
    class InputSource
    {
    public:
        InputSource() = default;
        ~InputSource();

        bool initialize(const PipelineConfig &config, PipelineContext &pipelineCtx);
        bool readPacket(AVPacket *pkt);

        // Is this packet a video packet?
        bool isVideoPacket(const AVPacket *pkt) const;

        // Is this packet an audio packet?
        bool isAudioPacket(const AVPacket *pkt) const;

    private:
        bool openInput();
        bool findStreams();

        void logInputInfo(InputInfo inputInfo);

        ContainerFormat detectContainerFormat();
        VideoCodecType detectVideoCodecFromStream();
        AudioCodecType detectAudioCodecFromStream();

        std::string inputPath_;
        SourceType inputType_;
        AVFormatContext *fmtCtx_ = nullptr;
        AVStream *videoStream_ = nullptr;
        AVStream *audioStream_ = nullptr;
    };
}