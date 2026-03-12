#include "output_sink.hpp"
#include <iostream>

extern "C"
{
#include <libavutil/mathematics.h>
}

namespace transcoder
{
    OutputSink::~OutputSink()
    {
        if (fmtCtx_)
        {
            if (headerWritten_ && !trailerWritten_)
                av_write_trailer(fmtCtx_);
            if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE))
                avio_closep(&fmtCtx_->pb);
            avformat_free_context(fmtCtx_);
            fmtCtx_ = nullptr;
            videoStream_ = nullptr;
            audioStream_ = nullptr;
            headerWritten_ = false;
        }
    }

    bool OutputSink::initialize(const PipelineConfig &config, PipelineContext &pipelineCtx)
    {
        std::string outputPath = config.outputPath;

        pipelineCfg_ = config;
        pipelineCtx_ = pipelineCtx;

        int ret = avformat_alloc_output_context2(&fmtCtx_, nullptr, nullptr, outputPath.c_str());
        if (ret < 0 || !fmtCtx_)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Output Sink] ERROR: Cannot create context for '" << outputPath << "': " << err << "\n";
            return false;
        }

        if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&fmtCtx_->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Output Sink] ERROR: Cannot open file: " << outputPath << ", " << err << "\n";
                return false;
            }
        }

        std::cout << "[Output Sink] Opened: " << outputPath << "\n";
        return true;
    }

    bool OutputSink::initVideoStream()
    {
        videoStream_ = avformat_new_stream(fmtCtx_, nullptr);

        if (!videoStream_)
        {
            std::cout << "[Output Sink] ERROR: Failed to create video stream\n";
            return false;
        }

        if (pipelineCtx_.videoTranscodingRequired)
        {
            int ret = avcodec_parameters_from_context(videoStream_->codecpar, &(pipelineCtx_.encoderInfo.videoEncoderCtx));
            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Output Sink] ERROR: Failed to copy video codec params: " << err << "\n";
                return false;
            }
        }
        else
        {
            int ret = avcodec_parameters_copy(videoStream_->codecpar, pipelineCtx_.inputInfo.videoStream.codecpar);
            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Output Sink] ERROR: Failed to copy video codec params: " << err << "\n";
                return false;
            }
        }

        videoStream_->codecpar->codec_tag = 0;
        videoStream_->avg_frame_rate = pipelineCfg_.keepOriginalFramerate ? pipelineCtx_.inputInfo.videoStream.avg_frame_rate : (AVRational){pipelineCfg_.targetFramerate, 1};
        videoStream_->time_base = av_inv_q(videoStream_->avg_frame_rate);
        // Store it for rescaling
        videoTimeBase_ = videoStream_->time_base;

        std::cout << "[Output Sink] Created video stream: " << avcodec_get_name(videoStream_->codecpar->codec_id) << " | "
                  << videoStream_->codecpar->width << "x" << videoStream_->codecpar->height << " | "
                  << av_q2d(videoStream_->avg_frame_rate) << " fps | "
                  << av_q2d(videoStream_->time_base) << " tb | "
                  << (videoStream_->codecpar->bit_rate) / 1000 << " kbps\n";

        return true;
    }

    bool OutputSink::initAudioStream()
    {
        audioStream_ = avformat_new_stream(fmtCtx_, nullptr);
        if (!audioStream_)
        {
            std::cout << "[Output Sink] ERROR: Failed to create audio stream\n";
            return false;
        }

        if (pipelineCtx_.audioTranscodingRequired)
        {
            int ret = avcodec_parameters_from_context(audioStream_->codecpar, &(pipelineCtx_.encoderInfo.audioEncoderCtx));
            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Output Sink] ERROR: Failed to copy audio codec params: " << err << "\n";
                return false;
            }
        }
        else
        {
            int ret = avcodec_parameters_copy(audioStream_->codecpar, pipelineCtx_.inputInfo.audioStream.codecpar);
            if (ret < 0)
            {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                std::cout << "[Output Sink] ERROR: Failed to copy audio codec params: " << err << "\n";
                return false;
            }
        }

        audioStream_->codecpar->codec_tag = 0;
        audioStream_->time_base = {1, audioStream_->codecpar->sample_rate};
        audioTimeBase_ = audioStream_->time_base;

        std::cout << "[Output Sink] Created audio stream: " << avcodec_get_name(audioStream_->codecpar->codec_id) << " | "
                  << audioStream_->codecpar->sample_rate << " Hz | "
                  << av_q2d(audioStream_->time_base) << " tb | "
                  << audioStream_->codecpar->ch_layout.nb_channels << " channels | "
                  << av_get_sample_fmt_name((AVSampleFormat)audioStream_->codecpar->format) << " sample format | "
                  << (audioStream_->codecpar->bit_rate) / 1000 << " kbps\n";

        return true;
    }

    bool OutputSink::writeHeader()
    {
        AVDictionary *opts = nullptr;
        int ret = avformat_write_header(fmtCtx_, &opts);
        av_dict_free(&opts);

        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Output Sink] Write header failed: " << err << "\n";
            return false;
        }

        // IMPORTANT: avformat_write_header() may CHANGE the stream time_base
        // after negotiation with the muxer. Re-read it so our rescaling is correct.
        if (videoStream_)
        {
            videoTimeBase_ = videoStream_->time_base;
            std::cout << "[Output Sink] Video stream after writing header: " << avcodec_get_name(videoStream_->codecpar->codec_id) << " | "
                      << videoStream_->codecpar->width << "x" << videoStream_->codecpar->height << " | "
                      << av_q2d(videoStream_->avg_frame_rate) << " fps | "
                      << av_q2d(videoStream_->time_base) << " tb | "
                      << (videoStream_->codecpar->bit_rate) / 1000 << " kbps\n";
        }
        if (audioStream_)
        {
            audioTimeBase_ = audioStream_->time_base;
            std::cout << "[Output Sink] Audio stream after writing header: " << avcodec_get_name(audioStream_->codecpar->codec_id) << " | "
                      << audioStream_->codecpar->sample_rate << " Hz | "
                      << av_q2d(audioStream_->time_base) << " tb | "
                      << audioStream_->codecpar->ch_layout.nb_channels << " channels | "
                      << av_get_sample_fmt_name((AVSampleFormat)audioStream_->codecpar->format) << " sample format | "
                      << (audioStream_->codecpar->bit_rate) / 1000 << " kbps\n";
        }

        headerWritten_ = true;

        return true;
    }

    bool OutputSink::writeTrailer()
    {
        if (headerWritten_)
            av_write_trailer(fmtCtx_);
        if (!(fmtCtx_->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmtCtx_->pb);
        trailerWritten_ = true;
        return true;
    }

    bool OutputSink::writeVideoPacket(AVPacket *pkt, AVRational srcTimeBase)
    {
        if (!headerWritten_ || !videoStream_)
            return false;

        av_packet_rescale_ts(pkt, srcTimeBase, videoTimeBase_);
        pkt->stream_index = videoStream_->index;

        int ret = av_interleaved_write_frame(fmtCtx_, pkt);

        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Output Sink] Video write error: " << err << "\n";
            return false;
        }
        return true;
    }

    bool OutputSink::writeAudioPacket(AVPacket *pkt, AVRational srcTimeBase)
    {
        if (!headerWritten_ || !audioStream_)
            return false;

        av_packet_rescale_ts(pkt, srcTimeBase, audioStream_->time_base);
        pkt->stream_index = audioStream_->index;

        int ret = av_interleaved_write_frame(fmtCtx_, pkt);
        if (ret < 0)
        {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            std::cout << "[Output Sink] Audio write error: " << err << "\n";
            return false;
        }
        return true;
    }
} // namespace transcoder
