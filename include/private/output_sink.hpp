#pragma once

#include "pipeline_context.hpp"
#include "transcoder.hpp"

#include <string>
#include <iostream>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

namespace transcoder
{

    class OutputSink
    {
    public:
        OutputSink() = default;
        ~OutputSink();

        bool initialize(const PipelineConfig &config, PipelineContext &pipelineCtx);

        bool initVideoStream();
        bool initAudioStream();
        bool writeHeader();
        bool writeTrailer();
        bool writeVideoPacket(AVPacket *pkt, AVRational srcTimeBase);
        bool writeAudioPacket(AVPacket *pkt, AVRational srcTimeBase);

        // bool rolloverSegment();

    private:
        AVFormatContext *fmtCtx_ = nullptr;
        AVStream *videoStream_ = nullptr;
        AVStream *audioStream_ = nullptr;
        PipelineContext pipelineCtx_;
        PipelineConfig pipelineCfg_;

        AVRational videoTimeBase_ = {1, 1};
        AVRational audioTimeBase_ = {1, 1};

        bool headerWritten_ = false;
        bool trailerWritten_ = false;

        // bool segmentEnabled_ = false;
        // int segmentDurationSecs_ = 0;

        // int64_t videoSegmentStartPts_ = AV_NOPTS_VALUE;
        // int64_t audioSegmentStartPts_ = AV_NOPTS_VALUE;

        // int64_t videoSegmentDurationPts_ = 0;
        // int64_t audioSegmentDurationPts_ = 0;

        // int segmentIndex_ = 0;
        // std::string segmentPattern_ = "";

        // // Add to private members:
        // bool audioNeedsRollover_ = false;

        // std::string buildSegmentPath() const;
        // std::string buildSegmentPattern(const std::string &path);
        // bool open(const std::string outputPath);
    };

} // namespace transcoder
