#include "video_encoder.hpp"
#include "transcoder.hpp"
#include "gpu_helper.hpp"

extern "C"
{
#include <libavutil/opt.h>
}

#include <iostream>

namespace transcoder
{
    VideoEncoder::~VideoEncoder()
    {
        if (videoCodecCtx_)
        {
            avcodec_free_context(&videoCodecCtx_);
            videoCodecCtx_ = nullptr;
        }
        if (gpuContext_.hw_device_ctx)
        {
            av_buffer_unref(&gpuContext_.hw_device_ctx);
        }
    }

    bool VideoEncoder::initialize(const PipelineConfig &config, PipelineContext &pipelineCtx)
    {
        if (config.hwDeviceType != HWDeviceType::NONE)
        {
            if (initHardware(config, pipelineCtx))
            {
                std::cout << "[Video Encoder] GPU context initialized successfully.\n";
                pipelineCtx.encoderInfo.videoEncoderCtx = (*videoCodecCtx_);
                return true;
            }

            gpuContext_.hw_device_ctx = nullptr;
            std::cout << "[Video Encoder] INFO: Failed to initialize gpu context, falling back to software encoder.\n";
        }

        if (!initSoftware(config, pipelineCtx))
            return false;

        pipelineCtx.encoderInfo.videoEncoderCtx = (*videoCodecCtx_);

        return true;
    }

    bool VideoEncoder::initHardware(const PipelineConfig &config, PipelineContext &pipelineCtx)
    {

        if (!gpuHelper_->initialize(gpuContext_, config.hwDeviceType))
        {
            std::cout << "[Video Encoder] ERROR : GPU helper initialization failed\n";
            return false;
        }

        std::string name = getEncoderName(config.hwDeviceType, config.targetVideoCodec);

        const AVCodec *codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec)
        {
            std::cout << "[Video Encoder] ERROR : Encoder Not found: " << name << "\n";
            return false;
        }

        videoCodecCtx_ = avcodec_alloc_context3(codec);
        videoCodecCtx_->width = pipelineCtx.inputInfo.videoStream.codecpar->width;
        videoCodecCtx_->height = pipelineCtx.inputInfo.videoStream.codecpar->height;
        videoCodecCtx_->pix_fmt = gpuContext_.pix_fmt;
        videoCodecCtx_->gop_size = 12;
        // videoCodecCtx_->max_b_frames = (config.targetVideoCodec == VideoCodecType::MJPEG) ? 0 : 2;
        videoCodecCtx_->max_b_frames = 0;
        videoCodecCtx_->framerate = config.keepOriginalFramerate ? pipelineCtx.inputInfo.videoStream.avg_frame_rate : ((AVRational){config.targetFramerate, 1});
        videoCodecCtx_->time_base = av_inv_q(videoCodecCtx_->framerate);
        applyBitrateSettings(config);

        videoCodecCtx_->hw_device_ctx = av_buffer_ref(gpuContext_.hw_device_ctx);

        if (config.hwDeviceType == HWDeviceType::VAAPI)
        {
            AVBufferRef *frames_ref = av_hwframe_ctx_alloc(gpuContext_.hw_device_ctx);
            AVHWFramesContext *fc = (AVHWFramesContext *)frames_ref->data;
            fc->format = AV_PIX_FMT_VAAPI;
            fc->sw_format = AV_PIX_FMT_NV12;
            fc->width = videoCodecCtx_->width;
            fc->height = videoCodecCtx_->height;
            fc->initial_pool_size = 20;

            int ret = av_hwframe_ctx_init(frames_ref);
            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Video Encoder] ERROR : Hardware frame context initialization failed : " << err << "\n";
                return false;
            }

            videoCodecCtx_->hw_frames_ctx = frames_ref;
        }

        // av_opt_set_int(enc_ctx->priv_data, "global_quality", 23, 0);

        AVDictionary *opts = nullptr;
        applyOptions(config.targetVideoCodec, &opts);

        videoCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int ret = avcodec_open2(videoCodecCtx_, codec, &opts);
        av_dict_free(&opts);

        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Video Encoder] ERROR : Open failed: " << err << "\n";
            return false;
        }

        std::cout << "[Video Encoder] Initialized: " << name << " | "
                  << videoCodecCtx_->width << "x" << videoCodecCtx_->height << " | "
                  << av_q2d(videoCodecCtx_->framerate) << " fps | "
                  << av_q2d(videoCodecCtx_->pkt_timebase) << " tb | "
                  << (videoCodecCtx_->bit_rate) / 1000 << " kbps\n";

        return true;
    }

    bool VideoEncoder::initSoftware(const PipelineConfig &config, PipelineContext &pipelineCtx)
    {
        std::string name = getEncoderName(config.targetVideoCodec);

        const AVCodec *codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec)
        {
            std::cout << "[Video Encoder] ERROR : Encoder Not found: " << name << "\n";
            return false;
        }

        videoCodecCtx_ = avcodec_alloc_context3(codec);
        videoCodecCtx_->width = pipelineCtx.inputInfo.videoStream.codecpar->width;
        videoCodecCtx_->height = pipelineCtx.inputInfo.videoStream.codecpar->height;
        videoCodecCtx_->pix_fmt = (config.targetVideoCodec == VideoCodecType::MJPEG) ? AV_PIX_FMT_YUVJ420P : AV_PIX_FMT_YUV420P;
        videoCodecCtx_->gop_size = 12;
        // videoCodecCtx_->max_b_frames = (config.targetVideoCodec == VideoCodecType::MJPEG) ? 0 : 2;
        videoCodecCtx_->max_b_frames = 0;
        videoCodecCtx_->framerate = config.keepOriginalFramerate ? pipelineCtx.inputInfo.videoStream.avg_frame_rate : ((AVRational){config.targetFramerate, 1});
        videoCodecCtx_->time_base = av_inv_q(videoCodecCtx_->framerate);

        applyBitrateSettings(config);

        AVDictionary *opts = nullptr;
        applyOptions(config.targetVideoCodec, &opts);

        videoCodecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        int ret = avcodec_open2(videoCodecCtx_, codec, &opts);
        av_dict_free(&opts);

        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Video Encoder] ERROR : Open failed: " << err << "\n";
            return false;
        }

        std::cout << "[Video Encoder] Initialized: " << name << " | "
                  << videoCodecCtx_->width << "x" << videoCodecCtx_->height << " | "
                  << av_q2d(videoCodecCtx_->framerate) << " fps | "
                  << av_q2d(videoCodecCtx_->pkt_timebase) << " tb | "
                  << (videoCodecCtx_->bit_rate) / 1000 << " kbps\n";

        return true;
    }

    void VideoEncoder::applyBitrateSettings(const PipelineConfig &config)
    {
        switch (config.bitrateMode)
        {
        case BitrateMode::CBR:
            videoCodecCtx_->bit_rate = config.targetBitrate;
            videoCodecCtx_->rc_min_rate = config.targetBitrate;
            videoCodecCtx_->rc_max_rate = config.targetBitrate;
            videoCodecCtx_->rc_buffer_size = config.targetBitrate;
            break;
        case BitrateMode::VBR:
            videoCodecCtx_->bit_rate = config.targetBitrate;
            break;
        case BitrateMode::CQP:
            videoCodecCtx_->global_quality = config.crf;
            videoCodecCtx_->flags |= AV_CODEC_FLAG_QSCALE;
            break;
        }
    }

    void VideoEncoder::applyOptions(VideoCodecType codec, AVDictionary **opts)
    {
        switch (codec)
        {
        case VideoCodecType::H264:
            // libx264
            av_dict_set(opts, "preset", "medium", 0);
            av_dict_set(opts, "tune", "film", 0);
            av_dict_set(opts, "profile", "high", 0);
            av_dict_set(opts, "movflags", "+faststart", 0); // safe for MP4
            break;

        case VideoCodecType::H265:
            // libx265
            av_dict_set(opts, "preset", "medium", 0);
            av_dict_set(opts, "x265-params", "log-level=none", 0);
            av_dict_set(opts, "profile", "main", 0);
            break;

        case VideoCodecType::VP9:
            // libvpx-vp9
            av_dict_set(opts, "deadline", "good", 0); // better than realtime
            av_dict_set(opts, "cpu-used", "4", 0);    // speed/quality balance
            av_dict_set(opts, "row-mt", "1", 0);      // enable row multithreading
            break;

        case VideoCodecType::AV1:
            // libaom-av1
            av_dict_set(opts, "cpu-used", "4", 0); // speed vs quality
            av_dict_set(opts, "row-mt", "1", 0);   // enable multithreading
            av_dict_set(opts, "usage", "good", 0); // balanced mode
            break;

        case VideoCodecType::MJPEG:
            // native mjpeg encoder
            av_dict_set(opts, "q:v", "3", 0); // quality scale (lower=better)
            break;

        case VideoCodecType::MPEG4:
            // native mpeg4 encoder
            av_dict_set(opts, "q:v", "5", 0); // quality-based encoding
            break;

        default:
            break;
        }
    }

    std::vector<AVPacket *> VideoEncoder::encodeFrame(AVFrame *frame)
    {
        AVFrame *frameToSend = frame;

        if (gpuContext_.is_gpu())
        {
            AVFrame *hw_frame = av_frame_alloc();
            // CPU frame needs uploading to GPU surface
            av_hwframe_get_buffer(videoCodecCtx_->hw_frames_ctx, hw_frame, 0);
            av_hwframe_transfer_data(hw_frame, frame, 0);
            hw_frame->pts = frame->pts;
            frameToSend = hw_frame;
        }

        int ret = avcodec_send_frame(videoCodecCtx_, frameToSend);

        if (frameToSend != frame)
            av_frame_unref(frameToSend);

        if (ret < 0 && ret != AVERROR(EAGAIN))
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Video Encoder] Send frame error : " << err << "\n";
            return {};
        }
        return receivePackets();
    }

    std::vector<AVPacket *> VideoEncoder::flush()
    {
        avcodec_send_frame(videoCodecCtx_, nullptr);
        return receivePackets();
    }

    std::vector<AVPacket *> VideoEncoder::receivePackets()
    {
        std::vector<AVPacket *> out;
        while (true)
        {
            AVPacket *pkt = av_packet_alloc();
            int ret = avcodec_receive_packet(videoCodecCtx_, pkt);
            if (ret == 0)
            {
                out.push_back(pkt);
            }
            else
            {
                av_packet_free(&pkt);
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    char err[256];
                    av_strerror(ret, err, sizeof(err));
                    std::cout << "[Video Encoder] ERROR: Receive packet error: " << err << "\n";
                }
                break;
            }
        }
        return out;
    }

    std::string VideoEncoder::getEncoderName(VideoCodecType codec)
    {
        switch (codec)
        {
        case VideoCodecType::H264:
            return "libx264";

        case VideoCodecType::H265:
            return "libx265";

        case VideoCodecType::MJPEG:
            return "mjpeg";

        case VideoCodecType::MPEG4:
            return "mpeg4";

        case VideoCodecType::VP9:
            return "libvpx-vp9";

        case VideoCodecType::AV1:
            return "libsvtav1";
            // return "libaom-av1";

        default:
            return "Unkown";
        }
    }

    std::string VideoEncoder::getEncoderName(HWDeviceType device, VideoCodecType codec)
    {
        switch (device)
        {
        case HWDeviceType::VAAPI:
            switch (codec)
            {
            case VideoCodecType::H264:
                return "h264_vaapi";
            case VideoCodecType::H265:
                return "hevc_vaapi";
            default:
                return "Unknown";
            }
        default:
            return "Unknown";
        }
    }
} // namespace transcoder
