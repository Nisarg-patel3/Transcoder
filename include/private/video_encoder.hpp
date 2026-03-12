#pragma once

#include "transcoder.hpp"
#include "pipeline_context.hpp"
#include "encoder.hpp"
#include "gpu_helper.hpp"

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
    class VideoEncoder : public Encoder
    {
    public:
        VideoEncoder() = default;
        ~VideoEncoder() override;

        bool initialize(const PipelineConfig &config, PipelineContext &pipelineCtx) override;

        std::vector<AVPacket *> encodeFrame(AVFrame *frame) override;
        std::vector<AVPacket *> flush() override;

    private:
        bool initSoftware(const PipelineConfig &config, PipelineContext &pipelineCtx);
        bool initHardware(const PipelineConfig &config, PipelineContext &pipelineCtx);

        void applyBitrateSettings(const PipelineConfig &config);
        void applyOptions(VideoCodecType codec, AVDictionary **opts);

        std::vector<AVPacket *> receivePackets();

        std::string getEncoderName(VideoCodecType codec);
        std::string getEncoderName(HWDeviceType device, VideoCodecType codec);

        std::unique_ptr<GpuHelper> gpuHelper_ = nullptr;
        GpuContext gpuContext_;

        AVCodecContext *videoCodecCtx_ = nullptr;
    };

} // namespace transcoder
