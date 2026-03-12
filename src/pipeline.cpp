#include "transcoder.hpp"
#include "config_validator.hpp"
#include "input_souce.hpp"
#include "video_decoder.hpp"
#include "audio_decoder.hpp"
#include "video_encoder.hpp"
#include "audio_encoder.hpp"
#include "frame_buffer.hpp"
#include "processing_stage.hpp"
#include "output_sink.hpp"

#include <iostream>

extern "C"
{
#include <libavutil/log.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
}

namespace transcoder
{
    Pipeline::Pipeline(const PipelineConfig &cfg) : config_(cfg)
    {
        av_log_set_level(AV_LOG_QUIET);
        context_ = new PipelineContext();
    }

    Pipeline::~Pipeline()
    {
        delete context_;
        stop();
        wait();
    }

    void Pipeline::setProcessingStage(std::unique_ptr<ProcessingStage> stage)
    {
        processor_ = std::move(stage);
    }

    bool Pipeline::initialize()
    {
        if (!ConfigValidator::validateInputOutputPaths(config_))
        {
            std::cout << "[Pipeline] ERROR: Initialization failed\n";
            return false;
        }

        if (!initInputSource())
        {
            std::cout << "[Pipeline] ERROR: Initialization failed\n";
            return false;
        }

        if (!ConfigValidator::validateConfiguration(config_, (*context_)))
        {
            std::cout << "[Pipeline] ERROR: Initialization failed\n";
            return false;
        }

        if (!initDecoder())
        {
            std::cout << "[Pipeline] ERROR: Initialization failed\n";
            return false;
        }

        // ── Processing Stage ────────────────────────────────────
        if (!processor_)
            processor_ = std::make_unique<PassThroughProcessor>();
        if (!processor_->initialize())
        {
            std::cout << "[Pipeline] Processing stage init failed\n";
            return false;
        }

        buffer_ = std::make_unique<FrameBuffer>(config_.queueMaxSize);

        if (!initEncoder())
        {
            std::cout << "[Pipeline] ERROR: Initialization failed\n";
            return false;
        }

        // if (!initOutputSink())
        // {
        //     std::cout << "[Pipeline] ERROR: Initialization failed\n";
        //     return false;
        // }

        monitor_ = std::make_unique<PerfMonitor>();

        std::cout << "[Pipeline] Initialized successfully\n";
        // logPipelineInfo();
        return true;
    }

    bool Pipeline::initInputSource()
    {
        input_ = std::make_unique<InputSource>();
        return input_->initialize(config_, (*context_));
    }

    bool Pipeline::initDecoder()
    {
        if ((*context_).videoTranscodingRequired)
        {
            videoDecoder_ = std::make_unique<VideoDecoder>();
            if (!videoDecoder_->initialize((*context_)))
                return false;
        }

        if ((*context_).audioTranscodingRequired)
        {
            audioDecoder_ = std::make_unique<AudioDecoder>();
            if (!audioDecoder_->initialize((*context_)))
                return false;
        }

        return true;
    }

    bool Pipeline::initEncoder()
    {
        if ((*context_).videoTranscodingRequired)
        {
            videoEncoder_ = std::make_unique<VideoEncoder>();
            if (!videoEncoder_->initialize(config_, (*context_)))
                return false;
        }

        if ((*context_).audioTranscodingRequired)
        {
            audioEncoder_ = std::make_unique<AudioEncoder>();
            if (!audioEncoder_->initialize(config_, (*context_)))
                return false;
        }

        return true;
    }

    bool Pipeline::initOutputSink(const std::unique_ptr<OutputSink> &output_)
    {
        if (!output_->initialize(config_, (*context_)))
            return false;

        if (!output_->initVideoStream())
            return false;

        if (config_.passAudio && (*context_).inputInfo.hasAudioStream)
        {
            if (!output_->initAudioStream())
                return false;
        }

        if (!output_->writeHeader())
            return false;

        return true;
    }

    bool Pipeline::run()
    {
        if (!start())
            return false;
        wait();
        return !encodeError_.load();
    }

    bool Pipeline::start()
    {
        if (running_.load())
            return false;
        running_.store(true);

        decoderThread_ = std::thread([this]
                                     { decoderLoop(); });
        encoderThread_ = std::thread([this]
                                     { encoderLoop(); });

        if (config_.enableStats)
        {
            monitorThread_ = std::thread([this]
                                         { monitorLoop(); });
        }

        return true;
    }

    void Pipeline::stop()
    {
        running_.store(false);
        if (buffer_)
            buffer_->shutdown();
    }

    void Pipeline::wait()
    {
        if (decoderThread_.joinable())
            decoderThread_.join();
        if (encoderThread_.joinable())
            encoderThread_.join();
        if (monitorThread_.joinable())
            monitorThread_.join();
    }

    // ─────────────────────────────────────────────────────────────
    // DECODER THREAD
    // ─────────────────────────────────────────────────────────────
    void Pipeline::decoderLoop()
    {
        AVPacket *pkt = av_packet_alloc();

        // Cache the audio stream time base here — used for audio rescaling
        AVRational audioTb;
        if (config_.passAudio && context_->inputInfo.hasAudioStream)
            audioTb = (*context_).inputInfo.audioStream.time_base;
        AVRational videoTb = (*context_).inputInfo.videoStream.time_base;

        int frameRate = (*context_).inputInfo.videoStream.avg_frame_rate.num;

        int count = 0;

        while (running_.load())
        {
            if (!input_->readPacket(pkt))
                break;

            if (input_->isAudioPacket(pkt) && config_.passAudio)
            {
                if (!(*context_).audioTranscodingRequired)
                {
                    if (outputs_.size() == 0)
                    {
                        outputs_.push_back(std::make_unique<OutputSink>());
                        initOutputSink(outputs_.back());
                    }
                    outputs_[0]->writeAudioPacket(pkt, audioTb);
                    av_packet_unref(pkt);
                    continue;
                }

                auto frames = audioDecoder_->decodePacket(pkt);

                for (AVFrame *frame : frames)
                {
                    if (!buffer_->push(frame))
                    {
                        av_frame_free(&frame);
                        goto decoder_done;
                    }
                }
            }

            // ── Video decode ──────────────────────────────────────
            else if (input_->isVideoPacket(pkt))
            {
                if (!(*context_).videoTranscodingRequired)
                {
                    if (outputs_.size() == 0)
                    {
                        outputs_.push_back(std::make_unique<OutputSink>());
                        initOutputSink(outputs_.back());
                    }
                    outputs_[0]->writeVideoPacket(pkt, videoTb);
                    av_packet_unref(pkt);
                    continue;
                }

                auto frames = videoDecoder_->decodePacket(pkt);

                count += frames.size();

                for (AVFrame *frame : frames)
                {
                    monitor_->recordFrameDecoded();

                    // Tag with wall-clock time for latency measurement
                    frame->opaque = reinterpret_cast<void *>(std::chrono::steady_clock::now().time_since_epoch().count());

                    if (!buffer_->push(frame))
                    {
                        av_frame_free(&frame);
                        goto decoder_done;
                    }
                }
            }

            av_packet_unref(pkt);
        }

        // Flush buffered B-frames from decoder
        {
            if (context_->videoTranscodingRequired)
            {
                auto frames = videoDecoder_->flush();
                for (AVFrame *frame : frames)
                {
                    monitor_->recordFrameDecoded();
                    if (!buffer_->push(frame))
                    {
                        av_frame_free(&frame);
                        break;
                    }
                }
            }
            if (context_->audioTranscodingRequired)
            {
                auto frames = audioDecoder_->flush();
                for (AVFrame *frame : frames)
                {
                    if (!buffer_->push(frame))
                    {
                        av_frame_free(&frame);
                        break;
                    }
                }
            }
        }

    decoder_done:
        av_packet_free(&pkt);
        decoderDone_.store(true);
        buffer_->shutdown();
        std::cout << "[Decoder] Done. Frames decoded: " << monitor_->totalFramesDecoded() << "\n";
    }

    // ─────────────────────────────────────────────────────────────
    // ENCODER THREAD
    // ─────────────────────────────────────────────────────────────
    void Pipeline::encoderLoop()
    {
        int tmpCount = 0;

        std::string baseOutputPath = config_.outputPath;

        int videoSegmentIdx = 0;
        int audioSegmentIdx = 0;

        int64_t videoFrameCounter = 0;
        int64_t audioFrameCounter = 0;

        AVRational audioTb;
        AVRational encTb = (*context_).encoderInfo.videoEncoderCtx.time_base;

        if (config_.passAudio && (*context_).inputInfo.hasAudioStream)
            audioTb = (*context_).encoderInfo.audioEncoderCtx.time_base;

        int frame_size = 0;
        if (config_.passAudio && context_->inputInfo.hasAudioStream)
        {
            frame_size = (*context_).encoderInfo.audioEncoderCtx.frame_size;
            if (frame_size <= 0)
                frame_size = 1024;
        }

        int64_t videoFramePerSegments = 0;
        int64_t audioFramePerSegments = 0;
        if (config_.segmentEnabled)
        {
            videoFramePerSegments = context_->inputInfo.videoStream.avg_frame_rate.num * config_.segmentDurationSeconds;
            if (config_.passAudio && context_->inputInfo.hasAudioStream)
                audioFramePerSegments = (context_->inputInfo.audioStream.codecpar->sample_rate * config_.segmentDurationSeconds);
        }

        SwrContext *swr = nullptr;
        AVCodecContext *audioCtx = nullptr;
        AVAudioFifo *fifo = nullptr;
        if (config_.passAudio && (*context_).audioTranscodingRequired)
        {
            swr = audioEncoder_->getSwrContext();
            audioCtx = &((*context_).encoderInfo.audioEncoderCtx);

            // ─────────────────────────────
            // Create FIFO
            // ─────────────────────────────
            fifo = av_audio_fifo_alloc(audioCtx->sample_fmt, audioCtx->ch_layout.nb_channels, 1024);
        }

        while (true)
        {
            AVFrame *frame = buffer_->pop();
            if (!frame)
                break;

            tmpCount++;

            // ───────────────── VIDEO ─────────────────
            if (frame->width > 0 && frame->height > 0)
            {
                auto decodeTimeNs = reinterpret_cast<int64_t>(frame->opaque);
                auto nowNs = std::chrono::steady_clock::now().time_since_epoch().count();
                monitor_->recordLatency((nowNs - decodeTimeNs) / 1000);
                monitor_->updateQueueDepth(buffer_->size());

                if (config_.segmentEnabled && (videoFrameCounter + 1) > videoFramePerSegments)
                {
                    std::cout << "[Framecounter] " << videoFrameCounter << " " << videoFramePerSegments << "\n";

                    auto flushPkts = videoEncoder_->flush();
                    for (AVPacket *pkt : flushPkts)
                    {
                        int bytes = pkt->size;
                        outputs_[videoSegmentIdx]->writeVideoPacket(pkt, encTb);
                        monitor_->recordFrameEncoded(bytes);
                        av_packet_free(&pkt);
                    }

                    if (videoSegmentIdx < audioSegmentIdx || !(config_.passAudio && context_->inputInfo.hasAudioStream))
                    {
                        outputs_[videoSegmentIdx]->writeTrailer();
                        outputs_[videoSegmentIdx].reset();
                    }

                    videoSegmentIdx++;
                    videoEncoder_.reset();
                    videoEncoder_ = std::make_unique<VideoEncoder>();
                    if (!videoEncoder_->initialize(config_, *context_))
                        break;
                    videoFrameCounter = 0;
                }

                if (videoFrameCounter == videoFramePerSegments - 1)
                {
                    frame->pict_type = AV_PICTURE_TYPE_I;
                    frame->flags |= AV_FRAME_FLAG_KEY;
                }
                frame->pts = videoFrameCounter;
                frame->pkt_dts = frame->pts;
                videoFrameCounter++;

                int64_t originalPts = frame->pts;

                AVFrame *processed = processor_ ? processor_->process(frame) : frame;

                if (processed)
                {
                    processed->pts = originalPts;
                    processed->pkt_dts = originalPts;
                }

                AVFrame *toEncode = processed;

                auto packets = videoEncoder_->encodeFrame(toEncode);

                if (toEncode != frame)
                    av_frame_free(&toEncode);
                av_frame_free(&frame);

                if (videoSegmentIdx >= outputs_.size())
                {
                    outputs_.push_back(std::make_unique<OutputSink>());
                    if (config_.segmentEnabled)
                        config_.outputPath = baseOutputPath.substr(0, baseOutputPath.find_last_of('.')) + std::to_string(videoSegmentIdx) + baseOutputPath.substr(baseOutputPath.find_last_of('.'));
                    initOutputSink(outputs_.back());
                }

                for (AVPacket *pkt : packets)
                {
                    int bytes = pkt->size;
                    outputs_[videoSegmentIdx]->writeVideoPacket(pkt, encTb);
                    monitor_->recordFrameEncoded(bytes);
                    av_packet_free(&pkt);
                }
            }

            // ───────────────── AUDIO ─────────────────
            else
            {
                // Resample
                AVFrame *resampled = av_frame_alloc();
                resampled->sample_rate = audioCtx->sample_rate;
                resampled->format = audioCtx->sample_fmt;
                av_channel_layout_copy(&resampled->ch_layout, &audioCtx->ch_layout);
                resampled->nb_samples = frame->nb_samples;

                av_frame_get_buffer(resampled, 0);

                swr_convert_frame(swr, resampled, frame);
                av_frame_free(&frame);

                // Expand FIFO if needed
                if (av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + resampled->nb_samples) < 0)
                {
                    std::cout << "[Encoder] FIFO realloc failed\n";
                }

                // Write samples to FIFO
                av_audio_fifo_write(fifo, (void **)resampled->data, resampled->nb_samples);

                av_frame_free(&resampled);

                // Encode while enough samples exist
                while (av_audio_fifo_size(fifo) >= frame_size)
                {
                    AVFrame *encFrame = av_frame_alloc();

                    encFrame->nb_samples = frame_size;
                    encFrame->format = audioCtx->sample_fmt;
                    encFrame->sample_rate = audioCtx->sample_rate;

                    av_channel_layout_copy(&encFrame->ch_layout, &audioCtx->ch_layout);
                    av_frame_get_buffer(encFrame, 0);

                    av_audio_fifo_read(fifo, (void **)encFrame->data, frame_size);

                    if (config_.segmentEnabled && (audioFrameCounter + 1) > audioFramePerSegments)
                    {
                        auto flushPkts = audioEncoder_->flush();
                        for (AVPacket *pkt : flushPkts)
                        {
                            outputs_[audioSegmentIdx]->writeAudioPacket(pkt, audioTb);
                            av_packet_free(&pkt);
                        }

                        if (audioSegmentIdx < videoSegmentIdx)
                        {
                            outputs_[audioSegmentIdx]->writeTrailer();
                            outputs_[audioSegmentIdx].reset();
                        }

                        audioSegmentIdx++;
                        audioEncoder_.reset();
                        audioEncoder_ = std::make_unique<AudioEncoder>();
                        if (!audioEncoder_->initialize(config_, *context_))
                            break;
                        audioFrameCounter = 0;
                    }

                    encFrame->pts = audioFrameCounter;
                    audioFrameCounter += encFrame->nb_samples;

                    auto packets = audioEncoder_->encodeFrame(encFrame);
                    av_frame_free(&encFrame);

                    if (audioSegmentIdx >= outputs_.size())
                    {
                        outputs_.push_back(std::make_unique<OutputSink>());
                        if (config_.segmentEnabled)
                            config_.outputPath = baseOutputPath.substr(0, baseOutputPath.find_last_of('.')) + std::to_string(audioSegmentIdx) + baseOutputPath.substr(baseOutputPath.find_last_of('.'));
                        initOutputSink(outputs_.back());
                    }

                    for (AVPacket *pkt : packets)
                    {
                        outputs_[audioSegmentIdx]->writeAudioPacket(pkt, audioTb);
                        av_packet_free(&pkt);
                    }
                }
            }
        }

        if (context_->audioTranscodingRequired)
        {
            // ─────────────────────────────
            // Flush remaining FIFO samples
            // ─────────────────────────────
            while (av_audio_fifo_size(fifo) > 0)
            {
                int samples = std::min(frame_size, av_audio_fifo_size(fifo));

                AVFrame *encFrame = av_frame_alloc();
                encFrame->nb_samples = samples;
                encFrame->format = audioCtx->sample_fmt;
                encFrame->sample_rate = audioCtx->sample_rate;

                av_channel_layout_copy(&encFrame->ch_layout, &audioCtx->ch_layout);
                av_frame_get_buffer(encFrame, 0);

                av_audio_fifo_read(fifo, (void **)encFrame->data, samples);

                if (config_.segmentEnabled && audioFrameCounter > audioFramePerSegments)
                {
                    auto flushPkts = audioEncoder_->flush();
                    for (AVPacket *pkt : flushPkts)
                    {
                        outputs_[audioSegmentIdx]->writeAudioPacket(pkt, audioTb);
                        av_packet_free(&pkt);
                    }

                    if (audioSegmentIdx < videoSegmentIdx)
                    {
                        outputs_[audioSegmentIdx]->writeTrailer();
                        outputs_[audioSegmentIdx].reset();
                    }

                    audioSegmentIdx++;
                    audioEncoder_.reset();
                    audioEncoder_ = std::make_unique<AudioEncoder>();
                    if (!audioEncoder_->initialize(config_, *context_))
                        break;
                    audioFrameCounter = 0;
                }

                encFrame->pts = audioFrameCounter;
                audioFrameCounter += encFrame->nb_samples;

                auto packets = audioEncoder_->encodeFrame(encFrame);
                av_frame_free(&encFrame);

                if (audioSegmentIdx >= outputs_.size())
                {
                    outputs_.push_back(std::make_unique<OutputSink>());
                    if (config_.segmentEnabled)
                        config_.outputPath = baseOutputPath.substr(0, baseOutputPath.find_last_of('.')) + std::to_string(audioSegmentIdx) + baseOutputPath.substr(baseOutputPath.find_last_of('.'));
                    initOutputSink(outputs_.back());
                }

                for (AVPacket *pkt : packets)
                {
                    outputs_[audioSegmentIdx]->writeAudioPacket(pkt, audioTb);
                    av_packet_free(&pkt);
                }
            }

            av_audio_fifo_free(fifo);
        }

        // ─────────────────────────────
        // Flush encoders
        // ─────────────────────────────
        if (context_->videoTranscodingRequired)
        {
            auto flushPkts = videoEncoder_->flush();
            for (AVPacket *pkt : flushPkts)
            {
                int bytes = pkt->size;
                outputs_[videoSegmentIdx]->writeVideoPacket(pkt, encTb);
                monitor_->recordFrameEncoded(bytes);
                av_packet_free(&pkt);
            }
        }

        if (context_->audioTranscodingRequired)
        {
            auto flushPkts = audioEncoder_->flush();
            for (AVPacket *pkt : flushPkts)
            {
                outputs_[audioSegmentIdx]->writeAudioPacket(pkt, audioTb);
                av_packet_free(&pkt);
            }
        }

        running_.store(false);
        std::cout << "[Encoder] Done. Frames encoded: "
                  << monitor_->totalFramesEncoded() << " " << tmpCount << "\n";
    }

    // ─────────────────────────────────────────────────────────────
    // MONITOR THREAD
    // ─────────────────────────────────────────────────────────────
    void Pipeline::monitorLoop()
    {
        while (running_.load())
        {
            monitor_->printStats();
            std::this_thread::sleep_for(std::chrono::seconds(config_.statsInterval));
        }
    }
}